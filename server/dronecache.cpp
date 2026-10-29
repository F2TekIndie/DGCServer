#include "dronecache.h"
#include "db.h"
#include "droneidentity.h"
#include <future>
#include "devices.pb.h"
#include "attributes.pb.h"
#include "timeseries.pb.h"
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <optional>
#include <algorithm>



//
namespace{
    void MergeDevice( Drone& drone, dronegroundcontrol::Device const& device ){
        if ( !ValidDroneId(device.id()) || !device.has_client_id() || !device.client_id().id() ){
            throw std::invalid_argument( "A valid device UUID and nonzero client ID are required" );
        }
        ValidateDroneName( device.name(), true );
        drone._droneId = device.id();
        drone._clientId = device.client_id().id();
        if ( !device.name().empty() ){
            drone._name = device.name();
        }
    }
    //
    void MergePosition( CacheData& entry, TimeseriesPos const& position ){
        if ( position._timestamp >= entry._Drone._position._timestamp
            || (entry._Drone._position._timestamp == std::chrono::sys_time<std::chrono::milliseconds>{}
                && entry._TimeSeries._position.empty()) ){
            entry._Drone._position = position;
        }
        auto found = std::find_if( entry._TimeSeries._position.begin(), entry._TimeSeries._position.end(),
            [&]( auto const& value ){
                return value._timestamp == position._timestamp;
            } );
        if ( found == entry._TimeSeries._position.end() ){
            entry._TimeSeries._position.push_back( position );
        }
        else if ( found->_posLat != position._posLat || found->_posLang != position._posLang ){
            *found = position;
            // A correction to stored history must be written again.
            entry._TimeSeries._persistedPositions = std::min( entry._TimeSeries._persistedPositions,
                static_cast<std::size_t>(found - entry._TimeSeries._position.begin()) );
        }
    }
    //
    double Coordinate( std::string const& text, double limit ){
        double value{};
        const auto parsed = std::from_chars( text.data(), text.data() + text.size(), value );
        if ( parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()
            || !std::isfinite( value ) || std::abs( value ) > limit ){
            throw std::invalid_argument( "Invalid position coordinate" );
        }
        return value;
    }
    //
    int64_t Timestamp( std::string const& text ){
        int64_t value{};
        const auto parsed = std::from_chars( text.data(), text.data() + text.size(), value );
        if ( parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ){
            throw std::invalid_argument( "Timestamp must be UTC Unix milliseconds" );
        }
        return value;
    }
}

//
DroneCache::DroneCache( std::shared_ptr<database> dbc )
    :_db( std::move( dbc ) )
{
    if ( !_db ){
        throw std::invalid_argument( "Database is required" );
    }
    // Startup hydration and all subsequent SQL access run on this one worker.
    std::promise<void> initialized;
    auto ready = initialized.get_future();
    tDBWorker = std::jthread( [this, initialized = std::move(initialized)]() mutable {
        try{
            for ( auto& [drone, history] : _db->LoadDrones() ){
                CacheData entry;
                entry._Drone = std::move(drone);
                entry._TimeSeries = std::move(history);
                _droneCache.emplace( entry._Drone._droneId, std::move(entry) );
            }
            initialized.set_value();
        }
        catch ( ... ){
            initialized.set_exception( std::current_exception() );
            return;
        }
        for ( ;; ){
            {
                std::unique_lock lock( _mutex );
                _wakeWorker.wait_for( lock, std::chrono::milliseconds(100), [this]{ return Exit(); } );
            }
            Synchronize();
            if ( Exit() ) break; // Final flush is performed by the worker as well.
            try{
                Refresh();
            }
            catch ( std::exception const& error ){
                std::cerr << "Cache refresh failed: " << error.what() << '\n';
            }
        }
    } );
    ready.get(); // Do not expose a partially hydrated cache on startup failure.
}

DroneCache::~DroneCache(){
    _shouldExit = true;
    _wakeWorker.notify_one();
    if ( tDBWorker.joinable() ) tDBWorker.join();
}

//
void DroneCache::StoreDevice( dronegroundcontrol::Device const& device ){
    const std::lock_guard lock( _mutex );
    // Build a candidate first so validation cannot leave a partial cache entry.
    Drone identity{};
    MergeDevice( identity, device );
    auto& entry = RequireDrone( device );
    MergeDevice( entry._Drone, device );
    entry._TimeSeries._droneId = entry._Drone._droneId;
    entry._TimeSeries._clientId = entry._Drone._clientId;
    entry._bDirty = true;
    ++entry._version;
}

//
void DroneCache::StoreAttribute( dronegroundcontrol::AttributeRequest const& request ){
    if ( request.has_attrib() == request.has_attributes()
        || (request.has_attributes() && request.attributes().timeseries().empty()) ){
        throw std::invalid_argument( "Provide one attribute or a nonempty attribute batch" );
    }
    Drone identity{};
    MergeDevice( identity, request.device() );
    // Validate and collect changes before locking; no history copies per update.
    std::optional<bool> online;
    std::optional<uint32_t> state;
    std::vector<TimeseriesPos> positions;
    auto apply = [&]( dronegroundcontrol::Attribute const& attribute ){
        if ( attribute.name() == "online" && attribute.has_bvalue() ){
            online = attribute.bvalue();
        }
        else if ( attribute.name() == "state" && attribute.has_ivalue() ){
            state = attribute.ivalue();
        }
        else if ( attribute.name() == "position" && attribute.has_pos() ){
            positions.push_back( {
                std::chrono::time_point_cast<std::chrono::milliseconds>( std::chrono::system_clock::now() ),
                Coordinate( attribute.pos().long_(), 180 ), Coordinate( attribute.pos().lat(), 90 )
            } );
        }
        else{
            throw std::invalid_argument( "Unsupported drone attribute or value type: " + attribute.name() );
        }
    };
    if ( request.has_attrib() ){
        apply( request.attrib() );
    }
    else{
        for ( auto const& attribute : request.attributes().timeseries() ){
            apply( attribute );
        }
    }
    const std::lock_guard lock( _mutex );
    auto& entry = RequireDrone( request.device() );
    MergeDevice( entry._Drone, request.device() );
    entry._TimeSeries._droneId = entry._Drone._droneId;
    entry._TimeSeries._clientId = entry._Drone._clientId;
    if ( online.has_value() ){
        entry._Drone._online = *online;
    }
    if ( state.has_value() ){
        entry._Drone._state = *state;
    }
    for ( auto const& position : positions ){
        MergePosition( entry, position );
    }
    entry._bDirty = true;
    ++entry._version;
}

//
void DroneCache::StoreTimeseries( dronegroundcontrol::TimeseriesRequest const& request ){
    if ( request.has_data() == request.has_timeseries()
        || (request.has_timeseries() && request.timeseries().timeseries().empty()) ){
        throw std::invalid_argument( "Provide one sample or a nonempty timeseries batch" );
    }
    Drone identity{};
    MergeDevice( identity, request.device() );
    // Pair coordinates within this message only; no pending position state.
    struct Coordinates{
        std::optional<double> _latitude;
        std::optional<double> _longitude;
    };
    std::map<int64_t, Coordinates> changes;
    auto apply = [&]( dronegroundcontrol::TimeSerie const& sample ){
        if ( !sample.has_svalue() ){
            throw std::invalid_argument( "Position samples require string coordinates" );
        }
        const auto stamp = Timestamp( sample.timestamp() );
        if ( sample.name() == "latitude" ){
            changes[stamp]._latitude = Coordinate( sample.svalue(), 90 );
        }
        else if ( sample.name() == "longitude" ){
            changes[stamp]._longitude = Coordinate( sample.svalue(), 180 );
        }
        else{
            throw std::invalid_argument( "Unsupported drone sample: " + sample.name() );
        }
    };
    if ( request.has_data() ){
        apply( request.data() );
    }
    else{
        for ( auto const& sample : request.timeseries().timeseries() ){
            apply( sample );
        }
    }
    std::vector<TimeseriesPos> positions;
    for ( auto const& [stamp, position] : changes ){
        if ( !position._latitude.has_value() || !position._longitude.has_value() ){
            throw std::invalid_argument( "Timeseries message must contain both coordinates for each timestamp" );
        }
        positions.push_back( {
            std::chrono::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds{stamp}},
            *position._longitude, *position._latitude
        } );
    }
    const std::lock_guard lock( _mutex );
    auto& entry = RequireDrone( request.device() );
    MergeDevice( entry._Drone, request.device() );
    entry._TimeSeries._droneId = entry._Drone._droneId;
    entry._TimeSeries._clientId = entry._Drone._clientId;
    for ( auto const& position : positions ){
        MergePosition( entry, position );
    }
    entry._bDirty = true;
    ++entry._version;
}

//
void DroneCache::Synchronize(){
    std::vector<CacheData> snapshots;
    std::set<std::string> deletions;
    {
        const std::lock_guard lock( _mutex );
        deletions = _deletedIds;
        for ( auto const& [id, entry] : _droneCache ){
            if ( entry._bDirty ) snapshots.push_back( entry );
        }
    }
    for ( auto& snapshot : snapshots ){
        auto const& id = snapshot._Drone._droneId;
        try{
            _db->StoreDrone( snapshot._Drone, snapshot._TimeSeries, snapshot._bNew );
            const std::lock_guard lock( _mutex );
            auto found = _droneCache.find( id );
            if ( found == _droneCache.end() ) continue; // Deleted during this write; tombstone wins next cycle.
            auto& entry = found->second;
            entry._bNew = false; // The identity committed even if newer telemetry arrived.
            if ( entry._version == snapshot._version ){
                entry._TimeSeries._persistedPositions = snapshot._TimeSeries._persistedPositions;
                entry._bDirty = false;
            }
        }
        catch ( std::exception const& error ){
            std::cerr << "Cache persistence failed for drone " << id << ": " << error.what() << '\n';
        }
    }
    // Also collect tombstones made while a snapshot was being written.
    {
        const std::lock_guard lock( _mutex );
        deletions.insert( _deletedIds.begin(), _deletedIds.end() );
    }
    for ( auto const& id : deletions ){
        try{
            _db->DeleteDrone( id );
            const std::lock_guard lock( _mutex );
            _deletedIds.erase( id );
        }
        catch ( std::exception const& error ){
            std::cerr << "Cache deletion failed for drone " << id << ": " << error.what() << '\n';
        }
    }
}

//
bool DroneCache::Exit(){
    return _shouldExit;
}

// Periodic discovery is independent of incoming requests. Load history only for new IDs.
void DroneCache::Refresh(){
    for ( auto const& id : _db->LoadDroneIds() ){
        {
            const std::lock_guard lock( _mutex );
            if ( _droneCache.contains(id) || _deletedIds.contains(id) ) continue;
        }
        for ( auto& [drone, history] : _db->LoadDrones(id) ){
            CacheData entry;
            entry._Drone = std::move(drone);
            entry._TimeSeries = std::move(history);
            const std::lock_guard lock( _mutex );
            if ( !_deletedIds.contains(id) ) _droneCache.try_emplace( id, std::move(entry) );
        }
    }
}

std::optional<CacheData> DroneCache::GetDrone( dronegroundcontrol::Device const& device ){
    if ( !ValidDroneId(device.id()) ){
        throw std::invalid_argument( "Device ID must be a canonical lowercase UUID" );
    }
    const std::lock_guard lock( _mutex );
    auto found = _droneCache.find( device.id() );
    if ( found == _droneCache.end()
        || (device.has_client_id() && device.client_id().id() != found->second._Drone._clientId) ){
        return std::nullopt;
    }
    return found->second;
}

std::vector<Drone> DroneCache::ListDrones( uint32_t clientId ){
    if ( !clientId ){
        throw std::invalid_argument( "Client ID must be nonzero" );
    }
    const std::lock_guard lock( _mutex );
    std::vector<Drone> drones;
    for ( auto const& [id, entry] : _droneCache ){
        if ( entry._Drone._clientId == clientId ){
            drones.push_back( entry._Drone );
        }
    }
    return drones;
}

Drone DroneCache::CreateDevice( uint32_t clientId ){
    if ( !clientId ) throw std::invalid_argument("Client ID must be nonzero");
    Drone drone{};
    drone._clientId = clientId;
    const std::lock_guard lock( _mutex );
    do{ drone._droneId = NewDroneId(); }
    while ( _droneCache.contains(drone._droneId) || _deletedIds.contains(drone._droneId) );
    drone._name = "Device_" + drone._droneId;
    CacheData entry;
    entry._Drone = drone;
    entry._TimeSeries._droneId = drone._droneId;
    entry._TimeSeries._clientId = drone._clientId;
    entry._bNew = true;
    entry._bDirty = true;
    _droneCache.emplace( drone._droneId, std::move(entry) );
    return drone;
}

bool DroneCache::DeleteDevice( dronegroundcontrol::Device const& device ){
    if ( !ValidDroneId(device.id()) ){
        throw std::invalid_argument( "Device ID must be a canonical lowercase UUID" );
    }
    const std::lock_guard lock( _mutex );
    auto found = _droneCache.find( device.id() );
    if ( found == _droneCache.end()
        || (device.has_client_id() && device.client_id().id() != found->second._Drone._clientId) ){
        return false;
    }
    _deletedIds.insert( device.id() );
    _droneCache.erase( found );
    return true;
}

CacheData& DroneCache::RequireDrone( dronegroundcontrol::Device const& device ){
    auto found = _droneCache.find( device.id() );
    if ( found == _droneCache.end() || found->second._Drone._clientId != device.client_id().id() ){
        throw std::out_of_range( "Drone not found" );
    }
    return found->second;
}

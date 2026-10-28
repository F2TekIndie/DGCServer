#include "dronecache.h"
#include "db.h"
#include "devices.pb.h"
#include "attributes.pb.h"
#include "timeseries.pb.h"
#include <charconv>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <optional>
#include <algorithm>



//
namespace{
    void MergeDevice( Drone& drone, dronegroundcontrol::Device const& device ){
        if ( !device.id() || !device.has_client_id() || !device.client_id().id() ){
            throw std::invalid_argument( "Device and client IDs must be nonzero" );
        }
        drone._droneId = device.id();
        drone._clientId = device.client_id().id();
        if ( !device.name().empty() ){
            drone._name = device.name();
        }
    }
    //
    void MergePosition( Drone& drone, TimeseriesPos const& position ){
        auto found = std::find_if( drone._position.begin(), drone._position.end(),
            [&]( auto const& value ){
                return value._timestamp == position._timestamp;
            } );
        if ( found == drone._position.end() ){
            drone._position.push_back( position );
        }
        else if ( found->_posLat != position._posLat || found->_posLang != position._posLang ){
            *found = position;
            // A correction to stored history must be written again.
            drone._persistedPositions = std::min( drone._persistedPositions,
                static_cast<std::size_t>(found - drone._position.begin()) );
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
    tDBWorker = std::jthread( [this](){
        while ( !Exit() ){
            std::this_thread::sleep_for( std::chrono::milliseconds(100) );
            Synchronize();
        }
    } );
}

//
DroneCache::~DroneCache(){
    _shouldExit = true;
    if ( tDBWorker.joinable() ){
        tDBWorker.join();
    }
    Synchronize(); // Final flush, after producers have stopped using the cache.
}

//
void DroneCache::StoreDevice( dronegroundcontrol::Device const& device ){
    const std::lock_guard lock( _mutex );
    // Build a candidate first so validation cannot leave a partial cache entry.
    Drone identity{};
    MergeDevice( identity, device );
    auto& entry = _droneCache[device.id()];
    MergeDevice( entry._Drone, device );
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
    auto& entry = _droneCache[identity._droneId];
    MergeDevice( entry._Drone, request.device() );
    if ( online.has_value() ){
        entry._Drone._online = *online;
    }
    if ( state.has_value() ){
        entry._Drone._state = *state;
    }
    for ( auto const& position : positions ){
        MergePosition( entry._Drone, position );
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
    auto& entry = _droneCache[identity._droneId];
    MergeDevice( entry._Drone, request.device() );
    for ( auto const& position : positions ){
        MergePosition( entry._Drone, position );
    }
    entry._bDirty = true;
    ++entry._version;
}

//
void DroneCache::Synchronize(){
    std::vector<std::pair<uint64_t, Drone>> snapshots;
    {
        const std::lock_guard lock( _mutex );
        for ( auto const& [id, entry] : _droneCache ){
            if ( entry._bDirty ){
                snapshots.emplace_back( entry._version, entry._Drone );
            }
        }
    }
    for ( auto& [version, drone] : snapshots ){
        try{
            _db->StoreDrone( drone ); // Advances persistence progress after commit.
            const std::lock_guard lock( _mutex );
            auto& entry = _droneCache.at( drone._droneId );
            // Preserve all history, including samples appended during this write.
            if ( entry._version == version ){
                entry._Drone._persistedPositions = drone._persistedPositions;
            }
            entry._bDirty = entry._version != version;
        }
        catch ( std::exception const& error ){
            std::cerr << "Cache persistence failed for drone " << drone._droneId << ": " << error.what() << '\n';
            // Keep dirty state and pending history for the next attempt.
        }
    }
}

//
bool DroneCache::Exit(){
    return _shouldExit;
}

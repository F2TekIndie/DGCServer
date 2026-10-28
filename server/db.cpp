#include "db.h"
#include "drone.h"
#include <sqlite3.h>
#include <cstdint>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <algorithm>
#include <limits>
#include <locale>
#include <sstream>
#include <charconv>
#include <map>



//
namespace{
    class Statement{
    public:
        Statement( sqlite3* db, char const* sql )
            :_db( db )
        {
            sqlite3_stmt* stmt = nullptr;
            const int rc = sqlite3_prepare_v2( db, sql, -1, &stmt, nullptr );
            _stmt.reset( stmt );
            if ( rc != SQLITE_OK ){
                Fail();
            }
        }
        //
        void Text( int index, std::string const& value ){
            if ( sqlite3_bind_text64( _stmt.get(), index, value.data(), value.size(),
                                      SQLITE_TRANSIENT, SQLITE_UTF8 ) != SQLITE_OK ){
                Fail();
            }
        }
        //
        void Integer( int index, sqlite3_int64 value ){
            if ( sqlite3_bind_int64( _stmt.get(), index, value ) != SQLITE_OK ){
                Fail();
            }
        }
        //
        void Real( int index, double value ){
            if ( sqlite3_bind_double( _stmt.get(), index, value ) != SQLITE_OK ){
                Fail();
            }
        }
        //
        bool Read(){
            const int result = sqlite3_step( _stmt.get() );
            if ( result == SQLITE_ROW ){
                return true;
            }
            if ( result != SQLITE_DONE ){
                Fail();
            }
            return false;
        }
        //
        sqlite3_int64 ColumnInteger( int index ){
            return sqlite3_column_int64( _stmt.get(), index );
        }
        //
        std::string ColumnText( int index ){
            auto text = sqlite3_column_text( _stmt.get(), index );
            return text ? std::string( reinterpret_cast<char const*>(text), sqlite3_column_bytes(_stmt.get(), index) ) : std::string{};
        }
        //
        double ColumnReal( int index ){
            return sqlite3_column_double( _stmt.get(), index );
        }
        //
        bool IsNull( int index ){
            return sqlite3_column_type( _stmt.get(), index ) == SQLITE_NULL;
        }
        //
        void Execute(){
            if ( sqlite3_step( _stmt.get() ) != SQLITE_DONE ){
                Fail();
            }
        }

    private:
        [[noreturn]] void Fail(){
            throw std::runtime_error( sqlite3_errmsg( _db ) );
        }
        sqlite3* _db;
        std::unique_ptr<sqlite3_stmt, decltype( &sqlite3_finalize )> _stmt{ nullptr, sqlite3_finalize };
    };

    //
    class Transaction{
    public:
        explicit Transaction( sqlite3* db, char const* begin = "BEGIN IMMEDIATE" )
            :_db( db )
        {
            Execute( begin );
        }
        //
        ~Transaction(){
            if ( !_committed ){
                sqlite3_exec( _db, "ROLLBACK", nullptr, nullptr, nullptr );
            }
        }
        //
        void Commit(){
            Execute( "COMMIT" );
            _committed = true;
        }

    private:
        void Execute( char const* sql ){
            if ( sqlite3_exec( _db, sql, nullptr, nullptr, nullptr ) != SQLITE_OK ){
                throw std::runtime_error( sqlite3_errmsg( _db ) );
            }
        }
        sqlite3* _db;
        bool _committed = false;
    };


}

//
database::database( Configuration const& config ){
    if ( sqlite3_open( config._dbName.c_str(), &_db ) != SQLITE_OK ){
        const std::string error = _db ? sqlite3_errmsg( _db ) : "SQLite allocation failed";
        sqlite3_close( _db );
        _db = nullptr;
        throw std::runtime_error( error );
    }
    if ( sqlite3_exec( _db, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr ) != SQLITE_OK ){
        const std::string error = sqlite3_errmsg( _db );
        sqlite3_close( _db );
        _db = nullptr;
        throw std::runtime_error( error );
    }
}

//
database::~database(){
    sqlite3_close( _db );
}

//
void database::StoreDrone( Drone const& drone, DroneTimeSeries& timeseries ){
    if ( drone._droneId != timeseries._droneId || drone._clientId != timeseries._clientId ){
        throw std::invalid_argument( "Drone and timeseries identity mismatch" );
    }
    if ( !std::isfinite( drone._position._posLat ) || std::abs( drone._position._posLat ) > 90
        || !std::isfinite( drone._position._posLang ) || std::abs( drone._position._posLang ) > 180 ){
        throw std::invalid_argument( "Invalid current position coordinate" );
    }
    if ( timeseries._persistedPositions > timeseries._position.size() ){
        throw std::invalid_argument( "Persisted position count exceeds history size" );
    }
    if ( !drone._droneId || !drone._clientId ){
        throw std::invalid_argument( "Device and client IDs must be nonzero" );
    }
    for ( auto const& position : timeseries._position ){
        if ( !std::isfinite( position._posLat ) || std::abs( position._posLat ) > 90
            || !std::isfinite( position._posLang ) || std::abs( position._posLang ) > 180 ){
            throw std::invalid_argument( "Invalid position coordinate" );
        }
    }
    //
    const std::lock_guard lock( _mutex );
    Transaction transaction( _db );
    // Accept new clients/devices without prior registration. Drone has no client
    // name or device type, so preserve that metadata on existing records.
    Statement client( _db, "INSERT INTO client(id,name) VALUES(?,?) ON CONFLICT(id) DO NOTHING" );
    client.Integer( 1, drone._clientId );
    client.Text( 2, "Client_" + std::to_string( drone._clientId ) );
    client.Execute();
    //
    Statement device( _db, "INSERT INTO device(id,client_id,name,type,position_timestamp_ms) VALUES(?,?,?,'',?) "
                          "ON CONFLICT(id) DO UPDATE SET client_id=excluded.client_id,position_timestamp_ms=excluded.position_timestamp_ms, "
                          "name=CASE WHEN ?<>'' THEN excluded.name ELSE device.name END" );
    device.Integer( 1, drone._droneId );
    device.Integer( 2, drone._clientId );
    device.Text( 3, drone._name.empty() ? "Device_" + std::to_string( drone._droneId ) : drone._name );
    device.Integer( 4, drone._position._timestamp.time_since_epoch().count() );
    device.Text( 5, drone._name );
    device.Execute();
    // Each statement starts with NULL value columns, clearing obsolete oneof
    // columns if an attribute previously had a different type.
    auto store = [&]( char const* name, char const* type ){
        Statement entry( _db,
            "INSERT INTO attributes(device_id,name,value_type,bool_value,uint32_value,latitude,longitude) "
            "VALUES(?,?,?,?,?,?,?) ON CONFLICT(device_id,name) DO UPDATE SET "
            "value_type=excluded.value_type,bool_value=excluded.bool_value,string_value=NULL,"
            "uint32_value=excluded.uint32_value,latitude=excluded.latitude,longitude=excluded.longitude" );
        entry.Integer( 1, drone._droneId );
        entry.Text( 2, name );
        entry.Text( 3, type );
        if ( std::string_view( name ) == "online" ){
            entry.Integer( 4, drone._online );
        }
        else if ( std::string_view( name ) == "state" ){
            entry.Integer( 5, drone._state );
        }
        else{
            entry.Real( 6, drone._position._posLat );
            entry.Real( 7, drone._position._posLang );
        }
        entry.Execute();
    };
    store( "online", "bool" );
    store( "state", "uint32" );
    store( "position", "position" );
    // Preserve the existing string-based coordinate history contract.
    auto storeSample = [&]( TimeseriesPos const& position, char const* name, double coordinate ){
        std::ostringstream text;
        text.imbue( std::locale::classic() );
        text.precision( std::numeric_limits<double>::max_digits10 );
        text << coordinate;
        Statement sample( _db,
            "INSERT INTO timeseries(device_id,name,timestamp_ms,value_type,string_value) VALUES(?,?,?,'string',?) "
            "ON CONFLICT(device_id,name,timestamp_ms) WHERE name IN ('latitude','longitude') DO UPDATE SET "
            "value_type=excluded.value_type,string_value=excluded.string_value,bool_value=NULL,uint32_value=NULL" );
        sample.Integer( 1, drone._droneId );
        sample.Text( 2, name );
        sample.Integer( 3, position._timestamp.time_since_epoch().count() );
        sample.Text( 4, text.str() );
        sample.Execute();
    };
    for ( auto idx = timeseries._persistedPositions; idx < timeseries._position.size(); ++idx ){
        auto const& position = timeseries._position[idx];
        storeSample( position, "latitude", position._posLat );
        storeSample( position, "longitude", position._posLang );
    }
    //
    transaction.Commit();
    timeseries._persistedPositions = timeseries._position.size();
}

//
std::vector<std::pair<Drone, DroneTimeSeries>> database::LoadDrones(){
    const std::lock_guard lock( _mutex );
    // Restore identities, attributes and history from a consistent snapshot.
    Transaction transaction( _db, "BEGIN" );
    std::vector<std::pair<Drone, DroneTimeSeries>> drones;
    std::map<uint32_t, std::size_t> indexes;
    Statement devices( _db,
        "SELECT d.id,d.client_id,d.name,o.bool_value,s.uint32_value,p.latitude,p.longitude,d.position_timestamp_ms FROM device d "
        "LEFT JOIN attributes o ON o.device_id=d.id AND o.name='online' AND o.value_type='bool' "
        "LEFT JOIN attributes s ON s.device_id=d.id AND s.name='state' AND s.value_type='uint32' "
        "LEFT JOIN attributes p ON p.device_id=d.id AND p.name='position' AND p.value_type='position' "
        "ORDER BY d.id" );
    while ( devices.Read() ){
        Drone drone{};
        drone._droneId = static_cast<uint32_t>( devices.ColumnInteger(0) );
        drone._clientId = static_cast<uint32_t>( devices.ColumnInteger(1) );
        drone._name = devices.ColumnText( 2 );
        drone._online = devices.ColumnInteger( 3 ) != 0;
        drone._state = static_cast<uint32_t>( devices.ColumnInteger(4) );
        indexes.emplace( drone._droneId, drones.size() );
        drone._position._posLat = devices.ColumnReal( 5 );
        drone._position._posLang = devices.ColumnReal( 6 );
        drone._position._timestamp = std::chrono::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds{devices.ColumnInteger(7)}};
        DroneTimeSeries history{};
        history._droneId = drone._droneId;
        history._clientId = drone._clientId;
        drones.emplace_back( std::move( drone ), std::move( history ) );
    }
    Statement positions( _db,
        "SELECT p.device_id,p.timestamp_ms,lat.string_value,lon.string_value "
        "FROM (SELECT DISTINCT device_id,timestamp_ms FROM timeseries WHERE name IN ('latitude','longitude')) p "
        "LEFT JOIN timeseries lat ON lat.device_id=p.device_id AND lat.timestamp_ms=p.timestamp_ms "
        "AND lat.name='latitude' AND lat.value_type='string' "
        "LEFT JOIN timeseries lon ON lon.device_id=p.device_id AND lon.timestamp_ms=p.timestamp_ms "
        "AND lon.name='longitude' AND lon.value_type='string' ORDER BY p.device_id,p.timestamp_ms" );
    auto coordinate = []( std::string const& text, double limit ){
        double value{};
        const auto result = std::from_chars( text.data(), text.data() + text.size(), value );
        if ( result.ec != std::errc{} || result.ptr != text.data() + text.size()
            || !std::isfinite( value ) || std::abs( value ) > limit ){
            throw std::runtime_error( "Invalid stored position coordinate" );
        }
        return value;
    };
    while ( positions.Read() ){
        const auto id = static_cast<uint32_t>( positions.ColumnInteger(0) );
        const auto found = indexes.find( id );
        if ( found == indexes.end() || positions.IsNull(2) || positions.IsNull(3) ){
            throw std::runtime_error( "Incomplete or orphaned stored position pair" );
        }
        drones[found->second].second._position.push_back( {
            std::chrono::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds{positions.ColumnInteger(1)}},
            coordinate( positions.ColumnText(3), 180 ), coordinate( positions.ColumnText(2), 90 )
        } );
    }
    for ( auto& [drone, history] : drones ){
        history._persistedPositions = history._position.size();
    }
    transaction.Commit();
    return drones;
}

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
        explicit Transaction( sqlite3* db )
            :_db( db )
        {
            Execute( "BEGIN IMMEDIATE" );
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
void database::StoreDrone( Drone& drone ){
    if ( drone._persistedPositions > drone._position.size() ){
        throw std::invalid_argument( "Persisted position count exceeds history size" );
    }
    if ( !drone._droneId || !drone._clientId ){
        throw std::invalid_argument( "Device and client IDs must be nonzero" );
    }
    for ( auto const& position : drone._position ){
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
    Statement device( _db, "INSERT INTO device(id,client_id,name,type) VALUES(?,?,?,'') "
                          "ON CONFLICT(id) DO UPDATE SET client_id=excluded.client_id, "
                          "name=CASE WHEN ?<>'' THEN excluded.name ELSE device.name END" );
    device.Integer( 1, drone._droneId );
    device.Integer( 2, drone._clientId );
    device.Text( 3, drone._name.empty() ? "Device_" + std::to_string( drone._droneId ) : drone._name );
    device.Text( 4, drone._name );
    device.Execute();
    // Each statement starts with NULL value columns, clearing obsolete oneof
    // columns if an attribute previously had a different type.
    auto latest = std::max_element( drone._position.begin(), drone._position.end(),
        []( auto const& left, auto const& right ){
            return left._timestamp < right._timestamp;
        } );
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
            entry.Real( 6, latest->_posLat );
            entry.Real( 7, latest->_posLang );
        }
        entry.Execute();
    };
    store( "online", "bool" );
    store( "state", "uint32" );
    if ( latest != drone._position.end() ){
        store( "position", "position" );
    }
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
    for ( auto idx = drone._persistedPositions; idx < drone._position.size(); ++idx ){
        auto const& position = drone._position[idx];
        storeSample( position, "latitude", position._posLat );
        storeSample( position, "longitude", position._posLang );
    }
    //
    transaction.Commit();
    drone._persistedPositions = drone._position.size();
}

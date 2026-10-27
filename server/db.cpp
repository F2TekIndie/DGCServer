#include "db.h"
#include "devices.pb.h"
#include "attributes.pb.h"
#include "timeseries.pb.h"
#include <sqlite3.h>
#include <charconv>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string_view>



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

    //
    void RequireName( std::string const& name ){
        if ( name.empty() || name.find_first_not_of( ' ' ) == std::string::npos ){
            throw std::invalid_argument( "Missing value name" );
        }
    }

    //
    void UpsertDevice( sqlite3* db, dronegroundcontrol::Device const& device ){
        if ( !device.id() || !device.has_client_id() || !device.client_id().id() ){
            throw std::invalid_argument( "Device and client IDs must be nonzero" );
        }
        // Unregistered clients/devices are accepted. Preserve existing metadata
        // when telemetry provides only IDs.
        Statement client( db, "INSERT INTO client(id,name) VALUES(?,?) "
                              "ON CONFLICT(id) DO UPDATE SET name=CASE WHEN ?<>'' THEN excluded.name ELSE client.name END" );
        client.Integer( 1, device.client_id().id() );
        client.Text( 2, device.client_id().name().empty()
                            ? "Client_" + std::to_string( device.client_id().id() )
                            : device.client_id().name() );
        client.Text( 3, device.client_id().name() );
        client.Execute();
        //
        Statement entry( db, "INSERT INTO device(id,client_id,name,type) VALUES(?,?,?,?) "
                             "ON CONFLICT(id) DO UPDATE SET client_id=excluded.client_id, "
                             "name=CASE WHEN ?<>'' THEN excluded.name ELSE device.name END, "
                             "type=CASE WHEN ?<>'' THEN excluded.type ELSE device.type END" );
        entry.Integer( 1, device.id() );
        entry.Integer( 2, device.client_id().id() );
        entry.Text( 3, device.name().empty() ? "Device_" + std::to_string( device.id() ) : device.name() );
        entry.Text( 4, device.type() );
        entry.Text( 5, device.name() );
        entry.Text( 6, device.type() );
        entry.Execute();
    }

    //
    double Coordinate( std::string const& text, double limit ){
        double result{};
        const auto parsed = std::from_chars( text.data(), text.data() + text.size(), result );
        if ( parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite( result ) || std::abs( result ) > limit ){
            throw std::invalid_argument( "Invalid position coordinate" );
        }
        return result;
    }
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
void database::StoreDevice( dronegroundcontrol::Device const& device ){
    const std::lock_guard lock( _mutex );
    Transaction transaction( _db );
    UpsertDevice( _db, device );
    transaction.Commit();
}

//
void database::StoreAttribute( dronegroundcontrol::AttributeRequest const& request ){
    const auto& value = request.attrib();
    RequireName( value.name() );
    //
    const std::lock_guard lock( _mutex );
    Transaction transaction( _db );
    UpsertDevice( _db, request.device() );
    //
    Statement entry( _db,
                     "INSERT INTO attributes(device_id,name,value_type,bool_value,string_value,uint32_value,latitude,longitude) "
                     "VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(device_id,name) DO UPDATE SET "
                     "value_type=excluded.value_type,bool_value=excluded.bool_value,string_value=excluded.string_value,"
                     "uint32_value=excluded.uint32_value,latitude=excluded.latitude,longitude=excluded.longitude" );
    entry.Integer( 1, request.device().id() );
    entry.Text( 2, value.name() );
    // Unbound parameters remain NULL, clearing the previous oneof value on update.
    switch ( value.value_case() ){
        case dronegroundcontrol::Attribute::kBValue:
            entry.Text( 3, "bool" );
            entry.Integer( 4, value.bvalue() );
            break;
        case dronegroundcontrol::Attribute::kSValue:
            entry.Text( 3, "string" );
            entry.Text( 5, value.svalue() );
            break;
        case dronegroundcontrol::Attribute::kIValue:
            entry.Text( 3, "uint32" );
            entry.Integer( 6, value.ivalue() );
            break;
        case dronegroundcontrol::Attribute::kPos:
            entry.Text( 3, "position" );
            entry.Real( 7, Coordinate( value.pos().lat(), 90 ) );
            entry.Real( 8, Coordinate( value.pos().long_(), 180 ) );
            break;
        default:
            throw std::invalid_argument( "Attribute has no value" );
    }
    //
    entry.Execute();
    transaction.Commit();
}

//
void database::StoreTimeseries( dronegroundcontrol::TimeseriesRequest const& request ){
    const auto& value = request.data();
    RequireName( value.name() );
    sqlite3_int64 timestamp{};
    const auto& text = value.timestamp();
    const auto parsed = std::from_chars( text.data(), text.data() + text.size(), timestamp );
    if ( parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ){
        throw std::invalid_argument( "Timestamp must be UTC Unix milliseconds" );
    }
    const std::lock_guard lock( _mutex );
    Transaction transaction( _db );
    UpsertDevice( _db, request.device() );
    //
    Statement entry( _db, "INSERT INTO timeseries(device_id,name,timestamp_ms,value_type,bool_value,string_value,uint32_value) "
                          "VALUES(?,?,?,?,?,?,?)" );
    entry.Integer( 1, request.device().id() );
    entry.Text( 2, value.name() );
    entry.Integer( 3, timestamp );
    switch ( value.value_case() ){
        case dronegroundcontrol::TimeSerie::kBValue:
            entry.Text( 4, "bool" );
            entry.Integer( 5, value.bvalue() );
            break;
        case dronegroundcontrol::TimeSerie::kSValue:
            entry.Text( 4, "string" );
            entry.Text( 6, value.svalue() );
            break;
        case dronegroundcontrol::TimeSerie::kIValue:
            entry.Text( 4, "uint32" );
            entry.Integer( 7, value.ivalue() );
            break;
        default:
            throw std::invalid_argument( "Timeseries sample has no value" );
    }
    //
    entry.Execute();
    transaction.Commit();
}

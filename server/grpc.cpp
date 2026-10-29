
#include "grpc.h"
#include <algorithm>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/status.h>
#include <memory>
#include <string>
#include "config.h"
#include "dronecache.h"
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>
#include "devices.grpc.pb.h"
#include "attributes.grpc.pb.h"
#include "timeseries.grpc.pb.h"



//
struct IServices{
    virtual ~IServices(){}
};

//
namespace{
    template<class Action>
    grpc::Status HandleRequest( Action&& action ){
        try{
            return action();
        }
        catch ( std::out_of_range const& error ){
            return {grpc::StatusCode::NOT_FOUND, error.what()};
        }
        catch ( std::invalid_argument const& error ){
            return {grpc::StatusCode::INVALID_ARGUMENT, error.what()};
        }
        catch ( std::exception const& ){
            return {grpc::StatusCode::INTERNAL, "Drone cache operation failed"};
        }
    }

    std::string CoordinateText( double value ){
        std::ostringstream text;
        text.imbue( std::locale::classic() );
        text.precision( std::numeric_limits<double>::max_digits10 );
        text << value;
        return text.str();
    }

    void FillPosition( TimeseriesPos const& position, dronegroundcontrol::Position* response ){
        response->set_lat( CoordinateText( position._posLat ) );
        response->set_long_( CoordinateText( position._posLang ) );
    }

    void FillDevice( Drone const& drone, dronegroundcontrol::Device* response ){
        response->Clear();
        response->set_id( drone._droneId );
        response->set_name( drone._name );
        response->mutable_client_id()->set_id( drone._clientId );
    }

    grpc::Status FillAttribute( Drone const& drone, std::string const& name,
                                dronegroundcontrol::Attribute* response ){
        response->Clear();
        response->set_name( name );
        if ( name == "online" ){
            response->set_bvalue( drone._online );
        }
        else if ( name == "state" ){
            response->set_ivalue( drone._state );
        }
        else if ( name == "position" ){
            FillPosition( drone._position, response->mutable_pos() );
        }
        else{
            return {grpc::StatusCode::INVALID_ARGUMENT, "Unknown attribute name"};
        }
        return grpc::Status::OK;
    }

    bool IsSeries( std::string const& name ){
        return name == "latitude" || name == "longitude";
    }

    void FillSample( TimeseriesPos const& position, std::string const& name,
                     dronegroundcontrol::TimeSerie* response ){
        response->Clear();
        response->set_name( name );
        response->set_timestamp( std::to_string(position._timestamp.time_since_epoch().count()) );
        response->set_svalue( CoordinateText(name == "latitude" ? position._posLat : position._posLang) );
    }

    //
    class DeviceService final : public IServices, public dronegroundcontrol::DeviceFunctions::Service {
    public:
        explicit DeviceService( std::shared_ptr<DroneCache> cache ) : _cache( std::move(cache) ){}
    private:
        std::shared_ptr<DroneCache> _cache;


        //
        grpc::Status CreateDevice( grpc::ServerContext* context,
                                    const dronegroundcontrol::Client* request,
                                    dronegroundcontrol::Device* response ) override {
            return HandleRequest( [&]() -> grpc::Status {
                FillDevice( _cache->CreateDevice(request->id()), response );
                return grpc::Status::OK;
            } );
        }
        
        //
        grpc::Status DeletDevice( grpc::ServerContext* context,
                                    const dronegroundcontrol::Device* request,
                                    dronegroundcontrol::DeviceStatusReply* response ) override {
            response->set_success( false );
            return HandleRequest( [&]() -> grpc::Status {
                if ( !_cache->DeleteDevice(*request) ){
                    return {grpc::StatusCode::NOT_FOUND, "Drone not found"};
                }
                response->set_success( true );
                return grpc::Status::OK;
            } );
        }
        
        //
        grpc::Status ListDevices(   grpc::ServerContext* context,
                                    const dronegroundcontrol::Client* request,
                                    dronegroundcontrol::Devices* response )override {
            return HandleRequest( [&]() -> grpc::Status {
                response->Clear();
                for ( auto const& drone : _cache->ListDrones( request->id() ) ){
                    FillDevice( drone, response->add_devicelist() );
                }
                response->set_range( response->devicelist_size() );
                return grpc::Status::OK;
            } );
        }
    };
    //
    class AttributeService final : public IServices, public dronegroundcontrol::AttributeFunctions::Service {
    public:
        explicit AttributeService( std::shared_ptr<DroneCache> cache ) : _cache( std::move(cache) ){}
    private:
        std::shared_ptr<DroneCache> _cache;

        grpc::Status UpdateAttribute(   grpc::ServerContext* context,
                                        const dronegroundcontrol::AttributeRequest* request,
                                        dronegroundcontrol::AttributeStatusReply* response )override {
            response->set_success( false );
            return HandleRequest( [&]() -> grpc::Status {
                _cache->StoreAttribute( *request );
                response->set_success( true );
                return grpc::Status::OK;
            } );
        }
        grpc::Status GetAttribute( grpc::ServerContext* context,
                                    const dronegroundcontrol::AttributeQuery* request,
                                    dronegroundcontrol::Attribute* response ) override {
            return HandleRequest( [&]() -> grpc::Status {
                if ( request->name() != "online" && request->name() != "state" && request->name() != "position" ){
                    return {grpc::StatusCode::INVALID_ARGUMENT, "Unknown attribute name"};
                }
                const auto entry = _cache->GetDrone( request->device() );
                if ( !entry ){
                    return {grpc::StatusCode::NOT_FOUND, "Drone not found"};
                }
                return FillAttribute( entry->_Drone, request->name(), response );
            } );
        }
        grpc::Status GetAttributes( grpc::ServerContext* context,
                                    const dronegroundcontrol::Device* request,
                                    dronegroundcontrol::Attributes* response )override {
            return HandleRequest( [&]() -> grpc::Status {
                const auto entry = _cache->GetDrone( *request );
                if ( !entry ){
                    return {grpc::StatusCode::NOT_FOUND, "Drone not found"};
                }
                response->Clear();
                for ( auto name : {"online", "state", "position"} ){
                    FillAttribute( entry->_Drone, name, response->add_timeseries() );
                }
                return grpc::Status::OK;
            } );
        }
        grpc::Status UpdatePosition(    grpc::ServerContext* context,
                                        const dronegroundcontrol::PositionRequest* request,
                                        dronegroundcontrol::AttributeStatusReply* response )override {
            response->set_success( false );
            return HandleRequest( [&]() -> grpc::Status {
                if ( !request->has_pos() ){
                    return {grpc::StatusCode::INVALID_ARGUMENT, "Position is required"};
                }
                dronegroundcontrol::AttributeRequest update;
                *update.mutable_device() = request->device();
                update.mutable_attrib()->set_name( "position" );
                *update.mutable_attrib()->mutable_pos() = request->pos();
                _cache->StoreAttribute( update );
                response->set_success( true );
                return grpc::Status::OK;
            } );
        }
        grpc::Status GetPosition(   grpc::ServerContext* context,
                                    const dronegroundcontrol::Device* request,
                                    dronegroundcontrol::Position* response )override {
            return HandleRequest( [&]() -> grpc::Status {
                const auto entry = _cache->GetDrone( *request );
                if ( !entry ){
                    return {grpc::StatusCode::NOT_FOUND, "Drone not found"};
                }
                FillPosition( entry->_Drone._position, response );
                return grpc::Status::OK;
            } );
        }
    };
    //
    class TimeseriesService final : public IServices, public dronegroundcontrol::TimeseriesFunctions::Service {
    public:
        explicit TimeseriesService( std::shared_ptr<DroneCache> cache ) : _cache( std::move(cache) ){}
    private:
        std::shared_ptr<DroneCache> _cache;

        grpc::Status AddTimeseries( grpc::ServerContext* context,
                                    const dronegroundcontrol::TimeseriesRequest* request,
                                    dronegroundcontrol::TimeseriesStatusReply* response )override {
            response->set_success( false );
            return HandleRequest( [&]() -> grpc::Status {
                _cache->StoreTimeseries( *request );
                response->set_success( true );
                return grpc::Status::OK;
            } );
        }
        grpc::Status GetTimeseries( grpc::ServerContext* context,
                                    const dronegroundcontrol::TimeseriesQuery* request,
                                    dronegroundcontrol::TimeSerie* response ) override {
            return HandleRequest( [&]() -> grpc::Status {
                if ( !IsSeries(request->name()) ){
                    return {grpc::StatusCode::INVALID_ARGUMENT, "Select latitude or longitude"};
                }
                const auto entry = _cache->GetDrone( request->device() );
                if ( !entry ){
                    return {grpc::StatusCode::NOT_FOUND, "Drone not found"};
                }
                TimeseriesPos const* selected = nullptr;
                for ( auto const& position : entry->_TimeSeries._position ){
                    const auto stamp = position._timestamp.time_since_epoch().count();
                    if ( request->has_timestamp_ms() ){
                        if ( stamp == request->timestamp_ms() ){
                            selected = &position;
                            break;
                        }
                    }
                    else if ( !selected || position._timestamp > selected->_timestamp ){
                        selected = &position;
                    }
                }
                if ( !selected ){
                    return {grpc::StatusCode::NOT_FOUND, "Sample not found"};
                }
                FillSample( *selected, request->name(), response );
                return grpc::Status::OK;
            } );
        }
        grpc::Status GetTimeseriesRange( grpc::ServerContext* context,
                                    const dronegroundcontrol::TimeseriesRangeQuery* request,
                                    dronegroundcontrol::TimeSeries* response ) override {
            return HandleRequest( [&]() -> grpc::Status {
                if ( !request->name().empty() && !IsSeries(request->name()) ){
                    return {grpc::StatusCode::INVALID_ARGUMENT, "Select latitude or longitude, or leave name empty"};
                }
                if ( request->has_start_timestamp_ms() && request->has_end_timestamp_ms()
                    && request->start_timestamp_ms() > request->end_timestamp_ms() ){
                    return {grpc::StatusCode::INVALID_ARGUMENT, "Range start exceeds end"};
                }
                auto entry = _cache->GetDrone( request->device() );
                if ( !entry ){
                    return {grpc::StatusCode::NOT_FOUND, "Drone not found"};
                }
                auto& positions = entry->_TimeSeries._position;
                std::sort( positions.begin(), positions.end(), []( auto const& lhs, auto const& rhs ){
                    return lhs._timestamp < rhs._timestamp;
                } );
                response->Clear();
                for ( auto const& position : positions ){
                    const auto stamp = position._timestamp.time_since_epoch().count();
                    if ( (request->has_start_timestamp_ms() && stamp < request->start_timestamp_ms())
                        || (request->has_end_timestamp_ms() && stamp > request->end_timestamp_ms()) ){
                        continue;
                    }
                    for ( auto name : {"latitude", "longitude"} ){
                        if ( request->name().empty() || request->name() == name ){
                            FillSample( position, name, response->add_timeseries() );
                        }
                    }
                }
                response->set_range( response->timeseries_size() );
                return grpc::Status::OK;
            } );
        }
    };
}

//
rpcServer::rpcServer( Configuration const& config, std::shared_ptr<DroneCache> db )
    :_config( config )
    ,_db( db )
    ,_server( nullptr )
    ,_services()
{
}

rpcServer::~rpcServer(){
    stop();
}

bool rpcServer::listen(){
    DeviceService* devService = new DeviceService( _db );
    _services.push_back( std::shared_ptr<IServices>( devService ) );
    AttributeService* attribService = new AttributeService( _db );
    _services.push_back( std::shared_ptr<IServices>( attribService ) );
    TimeseriesService* timeService = new TimeseriesService( _db );
    _services.push_back( std::shared_ptr<IServices>( timeService ) );
    //
    grpc::ServerBuilder builder;
    std::string sUri = std::get<0>(_config._grpcServer) + ":" + std::to_string( std::get<1>(_config._grpcServer) );
    builder.AddListeningPort( sUri, grpc::InsecureServerCredentials(), &_listeningPort );
    //
    builder.RegisterService( devService );
    builder.RegisterService( attribService );
    builder.RegisterService( timeService );
    //
    _server = builder.BuildAndStart();
    return (_server != nullptr);
}

void rpcServer::start(){
    auto runner = [this](){
        if ( _server ){
            _server->Wait();
        }
    };
    tRunner = std::jthread( runner );
}

void rpcServer::stop(){
    if ( _server ){
        _server->Shutdown();
    }
    if ( tRunner.joinable() ){
        tRunner.join();
    }
}

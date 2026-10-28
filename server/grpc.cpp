
#include "grpc.h"
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <cstddef>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/status.h>
#include <memory>
#include <string>
#include "config.h"
#include "devices.grpc.pb.h"
#include "attributes.grpc.pb.h"
#include "timeseries.grpc.pb.h"



//
struct IServices{
    virtual ~IServices(){}
};

//
namespace{
    //
    class DeviceService final : public IServices, public dronegroundcontrol::DeviceFunctions::Service {

        //
        grpc::Status CreateDevice(  grpc::ServerContext* context,
                                    const dronegroundcontrol::Client* request,
                                    dronegroundcontrol::Device* response ){
            // FROM DATABASE TO DGC REPLY
            ;
            return grpc::Status::OK;
        }
        
        //
        grpc::Status DeletDevice(   grpc::ServerContext* context,
                                    const dronegroundcontrol::Device* request,
                                    dronegroundcontrol::DeviceStatusReply* response ){
            // FROM DATABASE TO DGC REPLY
            ;
            return grpc::Status::OK;
        }
        
        //
        grpc::Status ListDevices(   grpc::ServerContext* context,
                                    const dronegroundcontrol::Client* request,
                                    dronegroundcontrol::Devices* response ){
            // FROM DATABASE TO DGC REPLY
            ;
            return grpc::Status::OK;
        }
    };
    //
    class AttributeService final : public IServices, public dronegroundcontrol::AttributeFunctions::Service {
        grpc::Status UpdateAttribute(   grpc::ServerContext* context,
                                        const dronegroundcontrol::AttributeRequest* request,
                                        dronegroundcontrol::AttributeStatusReply* response ){
            // FROM DATABASE TO DGC REPLY
            ;
            return grpc::Status::OK;
        }
        grpc::Status GetAttribute(  grpc::ServerContext* context,
                                    const dronegroundcontrol::Device* request,
                                    dronegroundcontrol::Attribute* response ){
            // FROM DATABASE TO DGC REPLY
            ;
            return grpc::Status::OK;
        }
        grpc::Status GetAttributes( grpc::ServerContext* context,
                                    const dronegroundcontrol::Device* request,
                                    dronegroundcontrol::Attributes* response ){
            // FROM DATABASE TO DGC REPLY
            ;
            return grpc::Status::OK;
        }
        grpc::Status UpdatePosition(    grpc::ServerContext* context,
                                        const dronegroundcontrol::PositionRequest* request,
                                        dronegroundcontrol::AttributeStatusReply* response ){
            // FROM DATABASE TO DGC REPLY
            ;
            return grpc::Status::OK;
        }
        grpc::Status GetPosition(   grpc::ServerContext* context,
                                    const dronegroundcontrol::Device* request,
                                    dronegroundcontrol::Position* response ){
            // FROM DATABASE TO DGC REPLY
            ;
            return grpc::Status::OK;
        }
    };
    //
    class TimeseriesService final : public IServices, public dronegroundcontrol::TimeseriesFunctions::Service {
        grpc::Status AddTimeseries( grpc::ServerContext* context,
                                    const dronegroundcontrol::TimeseriesRequest* request,
                                    dronegroundcontrol::TimeseriesStatusReply* response ){
            // FROM DATABASE TO DGC REPLY
            ;
            return grpc::Status::OK;
        }
        grpc::Status GetTimeseries( grpc::ServerContext* context,
                                    const dronegroundcontrol::Device* request,
                                    dronegroundcontrol::TimeSerie* response ){
            // FROM DATABASE TO DGC REPLY
            ;
            return grpc::Status::OK;
        }
        grpc::Status GetTimeseriesRange(    grpc::ServerContext* context,
                                            const dronegroundcontrol::Device* request,
                                            dronegroundcontrol::TimeSeries* response){
            // FROM DATABASE TO DGC REPLY
            ;
            return grpc::Status::OK;
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
    DeviceService* devService = new DeviceService();
    _services.push_back( std::shared_ptr<IServices>( devService ) );
    AttributeService* attribService = new AttributeService();
    _services.push_back( std::shared_ptr<IServices>( attribService ) );
    TimeseriesService* timeService = new TimeseriesService();
    _services.push_back( std::shared_ptr<IServices>( timeService ) );
    //
    grpc::ServerBuilder builder;
    std::string sUri = std::get<0>(_config._grpcServer) + ":" + std::to_string( std::get<1>(_config._grpcServer) );
    builder.AddListeningPort( sUri, grpc::InsecureServerCredentials() );
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

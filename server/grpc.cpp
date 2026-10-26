
#include "grpc.h"
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/status.h>
#include <string>
#include "config.h"
#include "devices.grpc.pb.h"
#include "attributes.grpc.pb.h"
#include "timeseries.grpc.pb.h"



//
namespace{
    //
    class DeviceService final : public dronegroundcontrol::DeviceFunctions::Service {

        //
        grpc::Status CreateDevice(  grpc::ServerContext* context,
                                    const dronegroundcontrol::Client* request,
                                    dronegroundcontrol::Device* response ){
            ;
            return grpc::Status::OK;
        }
        
        //
        grpc::Status DeletDevice(   grpc::ServerContext* context,
                                    const dronegroundcontrol::Device* request,
                                    dronegroundcontrol::DeviceStatusReply* response ){
            ;
            return grpc::Status::OK;
        }
        
        //
        grpc::Status ListDevices(   grpc::ServerContext* context,
                                    const dronegroundcontrol::Client* request,
                                    dronegroundcontrol::Devices* response ){
            ;
            return grpc::Status::OK;
        }
    };
    //
    class AttributeService final : public dronegroundcontrol::AttributeFunctions::Service {
        grpc::Status UpdateAttribute(   grpc::ServerContext* context,
                                        const dronegroundcontrol::AttributeRequest* request,
                                        dronegroundcontrol::AttributeStatusReply* response ){
            ;
            return grpc::Status::OK;
        }
        grpc::Status GetAttribute(  grpc::ServerContext* context,
                                    const dronegroundcontrol::Device* request,
                                    dronegroundcontrol::Attribute* response ){
            ;
            return grpc::Status::OK;
        }
        grpc::Status GetAttributes( grpc::ServerContext* context,
                                    const dronegroundcontrol::Device* request,
                                    dronegroundcontrol::Attributes* response ){
            ;
            return grpc::Status::OK;
        }
        grpc::Status UpdatePosition(    grpc::ServerContext* context,
                                        const dronegroundcontrol::PositionRequest* request,
                                        dronegroundcontrol::AttributeStatusReply* response ){
            ;
            return grpc::Status::OK;
        }
        grpc::Status GetPosition(   grpc::ServerContext* context,
                                    const dronegroundcontrol::Device* request,
                                    dronegroundcontrol::Position* response ){
            ;
            return grpc::Status::OK;
        }
    };
    //
    class TimeseriesService final : public dronegroundcontrol::TimeseriesFunctions::Service {
        grpc::Status AddTimeseries( grpc::ServerContext* context,
                                    const dronegroundcontrol::TimeseriesRequest* request,
                                    dronegroundcontrol::TimeseriesStatusReply* response ){
            ;
            return grpc::Status::OK;
        }
        grpc::Status GetTimeseries( grpc::ServerContext* context,
                                    const dronegroundcontrol::Device* request,
                                    dronegroundcontrol::TimeSerie* response ){
            ;
            return grpc::Status::OK;
        }
        grpc::Status GetTimeseriesRange(    grpc::ServerContext* context,
                                            const dronegroundcontrol::Device* request,
                                            dronegroundcontrol::TimeSeries* response){
            ;
            return grpc::Status::OK;
        }
    };
}

//
rpcServer::rpcServer( Configuration const& config, std::shared_ptr<database> db )
    :_config( config )
    ,_db( db )
    ,_server()
{
}

void rpcServer::listen(){
    DeviceService devService;
    AttributeService attribService;
    TimeseriesService timeService;
    //
    grpc::ServerBuilder builder;
    std::string sUri = std::get<0>(_config._grpcServer) + ":" + std::to_string( std::get<1>(_config._grpcServer) );
    builder.AddListeningPort( sUri, grpc::InsecureServerCredentials() );
    //
    builder.RegisterService( &devService );
    builder.RegisterService( &attribService );
    builder.RegisterService( &timeService );
    //
    _server = builder.BuildAndStart();
    //server->Wait(); // BLOCKING
}

void rpcServer::stop(){
    _server->Shutdown();
}

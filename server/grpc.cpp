
#include "grpc.h"
#include <grpcpp/grpcpp.h>
#include "devices.grpc.pb.h"



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

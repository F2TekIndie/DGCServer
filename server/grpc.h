
#pragma once

#include <grpcpp/server.h>
#include <memory>
#include <thread>



//
struct Configuration;
struct DroneCache;
struct IServices;

//
struct rpcServer{
    explicit rpcServer( Configuration const& config, std::shared_ptr<DroneCache> db );
    ~rpcServer();
    
    //
    bool listen();
    void start();
    void stop();

private:
    Configuration const& _config;
    std::shared_ptr<DroneCache> _db;
    std::vector< std::shared_ptr<IServices> > _services;
    std::unique_ptr<grpc::Server> _server;
    //
    std::jthread tRunner;    
};

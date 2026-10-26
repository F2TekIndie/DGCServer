
#pragma once

#include <grpcpp/server.h>
#include <memory>


//
struct Configuration;
struct database;

//
struct rpcServer{
    explicit rpcServer( Configuration const& config, std::shared_ptr<database> db );
    //
    void listen();
    void stop();

private:
    Configuration const& _config;
    std::shared_ptr<database> _db;
    std::unique_ptr<grpc::Server> _server;
};

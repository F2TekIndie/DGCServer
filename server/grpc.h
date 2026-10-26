
#pragma once

#include <grpcpp/server.h>
#include <memory>


//
struct Configuration;
struct database;
struct IServices;

//
struct rpcServer{
    explicit rpcServer( Configuration const& config, std::shared_ptr<database> db );
    //
    void listen();
    void stop();

private:
    Configuration const& _config;
    std::shared_ptr<database> _db;
    std::vector< std::shared_ptr<IServices> > _services;
    std::unique_ptr<grpc::Server> _server;
};

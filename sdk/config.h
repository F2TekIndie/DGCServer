
#pragma once
#include <string>



//
struct Configuration{
    //
    typedef std::tuple< std::string, uint > ServerConnection;
    //
    std::string _dbName = {"dgc.sqlite" };
    ServerConnection _mqttServer = {"127.0.0.1", 1883};
    ServerConnection _grpcServer = { "127.0.0.1", 5600 };
    bool _dbgLogging = {true};
};

//
inline Configuration loadConfiguration( std::string sFile ){
    Configuration conf;
    return conf;
}
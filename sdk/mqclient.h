
#pragma once
#include <boost/asio/io_context.hpp>
#include <boost/mqtt5/mqtt_client.hpp>
#include <boost/mqtt5/logger.hpp>
#include <memory>
#include <variant>



//
struct Configuration;

//
struct mqClient{
    explicit mqClient( Configuration const& config );
    //
    void connect();
    void disconnect();
    //
    void publish();
    void subscribe();
    
private:
    Configuration const& _config;
    boost::asio::io_context _ioc;
    //boost::mqtt5::mqtt_client<boost::asio::ip::tcp::socket, std::monostate, boost::mqtt5::logger> client;
    boost::mqtt5::mqtt_client<boost::asio::ip::tcp::socket> _client;
};

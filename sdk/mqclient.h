
#pragma once
#include <boost/asio/io_context.hpp>
#include <boost/mqtt5/mqtt_client.hpp>
#include <boost/mqtt5/logger.hpp>
#include <memory>
#include <string_view>
#include <variant>



//
struct Configuration;
struct ISubsciptionHandler{
    virtual ~ISubsciptionHandler(){};
    virtual std::vector<boost::mqtt5::subscribe_topic> Subscriptions() const = 0;
    virtual void HandleMessage( std::string sTopic, std::string sMessage ) = 0;
};

//
struct mqClient{
    explicit mqClient( Configuration const& config );
    //
    void connect();
    void disconnect();
    //
    void publish( std::string const& sTopic, std::string const& sMessage );
    void subscribe( std::shared_ptr<ISubsciptionHandler> pHandler );
    
private:
    Configuration const& _config;
    boost::asio::io_context _ioc;
    //boost::mqtt5::mqtt_client<boost::asio::ip::tcp::socket, std::monostate, boost::mqtt5::logger> client;
    boost::mqtt5::mqtt_client<boost::asio::ip::tcp::socket> _client;
    std::shared_ptr<ISubsciptionHandler> _subscriptionHandler = {nullptr};
};

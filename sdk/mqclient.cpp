
#include "mqclient.h"
#include "config.h"
#include "mqtt.h"
#include <boost/asio.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/as_tuple.hpp>




//
namespace{
    constexpr auto use_nothrow_awaitable = boost::asio::as_tuple(boost::asio::use_awaitable);
}

mqClient::mqClient( Configuration const& config )
    :_config( config )
    ,_ioc()
    ,_client( _ioc )
{
}

void mqClient::connect(){
    _client.brokers( std::get<0>(_config._mqttServer), std::get<1>(_config._mqttServer ) )
        .async_run( boost::asio::detached );
}

void mqClient::disconnect(){
    _client.async_disconnect( boost::asio::detached );
}

void mqClient::publish( std::string const& sTopic, std::string const& sMessage ){
    _client.async_publish<boost::mqtt5::qos_e::at_most_once>(
        sTopic, sMessage,
        boost::mqtt5::retain_e::yes, boost::mqtt5::publish_props {},
        [this](boost::mqtt5::error_code ec) {
            std::cout << ec.message() << std::endl;
            // Disconnnect the Client.
            _client.async_disconnect(boost::asio::detached);
        }
    );
}

void mqClient::subscribe( std::shared_ptr<ISubsciptionHandler> pHandler ){
    //
    _subscriptionHandler = pHandler;
    //
    auto subscribe = [this]() -> boost::asio::awaitable<bool> {
        // Configure the request to subscribe to a Topic.
        std::vector<boost::mqtt5::subscribe_topic> topics = _subscriptionHandler->Subscriptions();
        // Subscribe to a single Topic.
        auto&& [ec, sub_codes, sub_props] = co_await _client.async_subscribe(
            topics, boost::mqtt5::subscribe_props {}, use_nothrow_awaitable
        );
        // Note: you can subscribe to multiple Topics in one mqtt_client::async_subscribe call.
        if (ec)
            std::cout << "Subscribe error occurred: " << ec.message() << std::endl;
        else
            std::cout << "Result of subscribe request: " << sub_codes[0].message() << std::endl;

        co_return !ec && !sub_codes[0]; // True if the subscription was successfully established.
    };
    //
    auto subscribe_and_receive = [this, subscribe]() -> boost::asio::awaitable<void> {
        //
        if ( !(co_await subscribe()) )
            co_return;

        for (;;) {
            // Receive an Appplication Message from the subscribed Topic(s).
            auto&& [ec, topic, payload, publish_props] = co_await _client.async_receive(use_nothrow_awaitable);

            if (ec == boost::mqtt5::client::error::session_expired) {
                // The Client has reconnected, and the prior session has expired.
                // As a result, any previous subscriptions have been lost and must be reinstated.
                if ( co_await subscribe() )
                    continue;
                else
                    break;
            }
            else if (ec)
                break;

            std::cout << "Received message from the Broker" << std::endl;
            _subscriptionHandler->HandleMessage( topic, payload );
        }

        co_return;
    };
    //
    boost::asio::co_spawn(
        _ioc,
        std::move(subscribe_and_receive),
        [](std::exception_ptr e) {
            if (e)
                std::rethrow_exception(e);
        }
    );
}

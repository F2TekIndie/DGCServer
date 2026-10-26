
#include "mqclient.h"
#include "config.h"
#include "mqtt.h"
#include <boost/asio/detached.hpp>



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

/*
    _client.async_publish<boost::mqtt5::qos_e::at_most_once>(
        sTopic, sMessage,
        boost::mqtt5::retain_e::yes, boost::mqtt5::publish_props {},
        [&client](boost::mqtt5::error_code ec) {
            std::cout << ec.message() << std::endl;
            // Disconnnect the Client.
            client.async_disconnect(boost::asio::detached);
        }
    );
*/

/*
boost::asio::awaitable<bool> subscribe(client_type& client) {
    // Configure the request to subscribe to a Topic.
    boost::mqtt5::subscribe_topic sub_topic = boost::mqtt5::subscribe_topic{
        sTopic,
        boost::mqtt5::subscribe_options {
            boost::mqtt5::qos_e::exactly_once, // All messages will arrive at QoS 2.
            boost::mqtt5::no_local_e::no, // Forward message from Clients with same ID.
            boost::mqtt5::retain_as_published_e::retain, // Keep the original RETAIN flag.
            boost::mqtt5::retain_handling_e::send // Send retained messages when the subscription is established.
        }
    };

    // Subscribe to a single Topic.
    auto&& [ec, sub_codes, sub_props] = co_await client.async_subscribe(
        sub_topic, boost::mqtt5::subscribe_props {}, use_nothrow_awaitable
    );
    // Note: you can subscribe to multiple Topics in one mqtt_client::async_subscribe call.

    if (ec)
        std::cout << "Subscribe error occurred: " << ec.message() << std::endl;
    else
        std::cout << "Result of subscribe request: " << sub_codes[0].message() << std::endl;

    co_return !ec && !sub_codes[0]; // True if the subscription was successfully established.
}

boost::asio::awaitable<void> subscribe_and_receive(
    const config& cfg, client_type& client
) {
    if (!(co_await subscribe(client)))
        co_return;

    for (;;) {
        // Receive an Appplication Message from the subscribed Topic(s).
        auto&& [ec, topic, payload, publish_props] = co_await client.async_receive(use_nothrow_awaitable);

        if (ec == boost::mqtt5::client::error::session_expired) {
            // The Client has reconnected, and the prior session has expired.
            // As a result, any previous subscriptions have been lost and must be reinstated.
            if (co_await subscribe(client))
                continue;
            else
                break;
        } else if (ec)
            break;

        std::cout << "Received message from the Broker" << std::endl;
        std::cout << "\t topic: " << topic << std::endl;
        std::cout << "\t payload: " << payload << std::endl;
    }

    co_return;
}

    co_spawn(
        ioc,
        subscribe_and_receive(cfg, client),
        [](std::exception_ptr e) {
            if (e)
                std::rethrow_exception(e);
        }
    );
*/

#include "mqclient.h"
#include "config.h"
#include <boost/asio.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/mqtt5/logger.hpp>
#include <thread>
#include <variant>




//
namespace{
    constexpr auto use_nothrow_awaitable = boost::asio::as_tuple(boost::asio::use_awaitable);
}

mqClient::mqClient( Configuration const& config )
    :_config( config )
    ,_ioc()
    ,_client(   _ioc
                ,std::monostate{},
                boost::mqtt5::logger{ config._dbgLogging ? boost::mqtt5::log_level::debug : boost::mqtt5::log_level::error }
            )
{
}

mqClient::~mqClient(){
    stop();
}

void mqClient::connect(){
    _client.brokers( std::get<0>(_config._mqttServer), std::get<1>(_config._mqttServer ) )
        .async_run( boost::asio::detached );
}

void mqClient::disconnect(){
    boost::asio::post( _ioc, [this](){
        _client.async_disconnect( boost::asio::detached );
    });
}

void mqClient::publish( std::string const& sTopic, std::string const& sMessage ){
    boost::asio::post( _ioc, [this, sTopic, sMessage](){
        _client.async_publish<boost::mqtt5::qos_e::exactly_once>(
            sTopic, sMessage,
            boost::mqtt5::retain_e::yes, boost::mqtt5::publish_props {},
            []( boost::mqtt5::error_code ec, boost::mqtt5::reason_code reason,
                boost::mqtt5::pubcomp_props ){
                if ( ec ){
                    std::cout << ec.message() << std::endl;
                }
                else if ( reason ){
                    std::cout << reason.message() << std::endl;
                }
            }
        );
    });
}

void mqClient::subscribe( std::shared_ptr<ISubsciptionHandler> pHandler ){
    boost::asio::post( _ioc, [this, handler = std::move(pHandler)](){
        _subscriptionHandler = handler;
        //
        auto subscribe = [this]() -> boost::asio::awaitable<bool> {
            // Configure the request to subscribe to a Topic.
            std::vector<boost::mqtt5::subscribe_topic> topics = _subscriptionHandler->Subscriptions();
            // Subscribe to a single Topic.
            auto&& [ec, sub_codes, sub_props] = co_await _client.async_subscribe(
                topics, boost::mqtt5::subscribe_props {}, use_nothrow_awaitable
            );
            // Note: you can subscribe to multiple Topics in one mqtt_client::async_subscribe call.
            if ( ec ){
                std::cout << "Subscribe error occurred: " << ec.message() << std::endl;
            }
            else{
                for ( auto idx{0};idx<sub_codes.size();++idx){
                    std::cout << "Result of subscribe request: " << sub_codes[idx].message() << std::endl;
                }
            }

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
                else if ( ec ){
                    break;
                }
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
    });
}

void mqClient::start(){
    auto runner = [this](boost::asio::io_context& ioc ){
        ioc.run();
    };
    tRunner = std::jthread( runner, std::ref(_ioc) );
}

void mqClient::stop(){
    _ioc.stop();
    //
    if ( tRunner.joinable() ){
        tRunner.join();
    }
}


#include "mqsubscriptions.h"
#include "mqtt.h"
#include "db.h"
#include "devices.pb.h"
#include "attributes.pb.h"
#include "timeseries.pb.h"
#include <iostream>
#include <stdexcept>



dgcSubscriptions::dgcSubscriptions( std::shared_ptr<database> dbc  )
    :_dbc( dbc )
{
    if (!_dbc) throw std::invalid_argument("Database is required");
}

dgcSubscriptions::~dgcSubscriptions()
{
}

std::vector<boost::mqtt5::subscribe_topic> dgcSubscriptions::Subscriptions() const{
    std::vector<boost::mqtt5::subscribe_topic> topics = {
        {dronesdk::mqtt::EP_DEVICE, boost::mqtt5::subscribe_options {boost::mqtt5::qos_e::exactly_once, boost::mqtt5::no_local_e::no, boost::mqtt5::retain_as_published_e::dont, boost::mqtt5::retain_handling_e::send}}
        ,{dronesdk::mqtt::EP_STATUS, boost::mqtt5::subscribe_options {boost::mqtt5::qos_e::exactly_once, boost::mqtt5::no_local_e::no, boost::mqtt5::retain_as_published_e::dont, boost::mqtt5::retain_handling_e::send}}
        ,{dronesdk::mqtt::EP_ATTRIBUTES, boost::mqtt5::subscribe_options {boost::mqtt5::qos_e::exactly_once, boost::mqtt5::no_local_e::no, boost::mqtt5::retain_as_published_e::dont, boost::mqtt5::retain_handling_e::send}}
        ,{dronesdk::mqtt::EP_TIMESERIES, boost::mqtt5::subscribe_options {boost::mqtt5::qos_e::exactly_once, boost::mqtt5::no_local_e::no, boost::mqtt5::retain_as_published_e::dont, boost::mqtt5::retain_handling_e::send}}
    };
    return topics;
}

void dgcSubscriptions::HandleMessage( std::string sTopic, std::string sMessage ){
    try {
        if ( sTopic == dronesdk::mqtt::EP_DEVICE ){
            HandleDevice(sMessage);
        }
        else if ( sTopic == dronesdk::mqtt::EP_ATTRIBUTES ){
            HandleAttributes(sMessage);
        }
        else if ( sTopic == dronesdk::mqtt::EP_TIMESERIES ){
            HandleTimeseries(sMessage);
        }
        else {
            std::cerr << "Ignoring unsupported MQTT topic: " << sTopic << '\n';
        }
    }
    catch ( std::exception const& error ) {
        // Reject this message without taking down the MQTT receive loop.
        std::cerr << "MQTT message rejected on " << sTopic << ": " << error.what() << '\n';
    }
}

void dgcSubscriptions::HandleDevice( std::string const& sMessage ){
    dronegroundcontrol::Device message;
    if ( !message.ParseFromString(sMessage) ){
        throw std::invalid_argument("Invalid Device protobuf");
    }
    _dbc->StoreDevice( message );
}

void dgcSubscriptions::HandleAttributes( std::string const& sMessage ){
    dronegroundcontrol::AttributeRequest message;
    if ( !message.ParseFromString(sMessage) ){
        throw std::invalid_argument("Invalid AttributeRequest protobuf");
    }
    _dbc->StoreAttribute( message );
}

void dgcSubscriptions::HandleTimeseries( std::string const& sMessage ){
    dronegroundcontrol::TimeseriesRequest message;
    if ( !message.ParseFromString(sMessage) ){
        throw std::invalid_argument("Invalid TimeseriesRequest protobuf");
    }
    _dbc->StoreTimeseries( message );
}

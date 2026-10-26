
#include "mqsubscriptions.h"
#include "mqtt.h"
#include "db.h"



dgcSubscriptions::dgcSubscriptions( std::shared_ptr<database> dbc  )
    :_dbc( dbc )
{
}

dgcSubscriptions::~dgcSubscriptions()
{
}

std::vector<boost::mqtt5::subscribe_topic> dgcSubscriptions::Subscriptions() const{
    std::vector<boost::mqtt5::subscribe_topic> topics = {
        {dronesdk::mqtt::EP_DEVICE, boost::mqtt5::subscribe_options {boost::mqtt5::qos_e::exactly_once, boost::mqtt5::no_local_e::no, boost::mqtt5::retain_as_published_e::retain, boost::mqtt5::retain_handling_e::send}}
        ,{dronesdk::mqtt::EP_STATUS, boost::mqtt5::subscribe_options {boost::mqtt5::qos_e::exactly_once, boost::mqtt5::no_local_e::no, boost::mqtt5::retain_as_published_e::retain, boost::mqtt5::retain_handling_e::send}}
        ,{dronesdk::mqtt::EP_ATTRIBUTES, boost::mqtt5::subscribe_options {boost::mqtt5::qos_e::exactly_once, boost::mqtt5::no_local_e::no, boost::mqtt5::retain_as_published_e::retain, boost::mqtt5::retain_handling_e::send}}
        ,{dronesdk::mqtt::EP_TIMESERIES, boost::mqtt5::subscribe_options {boost::mqtt5::qos_e::exactly_once, boost::mqtt5::no_local_e::no, boost::mqtt5::retain_as_published_e::retain, boost::mqtt5::retain_handling_e::send}}
    };
    return topics;
}

void dgcSubscriptions::HandleMessage( std::string sTopic, std::string sMessage ){
    // FROM BROKER TO DATABASE
    //
}


#pragma once

#include "mqclient.h"
#include <boost/asio.hpp>
#include <boost/mqtt5.hpp>
#include <string>
#include <vector>
#include <memory>


//
struct DroneCache;

//
struct dgcSubscriptions : public ISubsciptionHandler{
    //
    explicit dgcSubscriptions( std::shared_ptr<DroneCache> dbc );
    ~dgcSubscriptions();
    //
    std::vector<boost::mqtt5::subscribe_topic> Subscriptions() const;
    void HandleMessage( std::string sTopic, std::string sMessage );

private:
    void HandleDevice( std::string const& sMessage );
    void HandleAttributes( std::string const& sMessage );
    void HandleTimeseries( std::string const& sMessage );
    //
    std::shared_ptr<DroneCache> _dbc;
};

#pragma once

#include "mqclient.h"
#include "db.h"
#include <boost/asio.hpp>
#include <boost/mqtt5.hpp>
#include <string>
#include <vector>
#include <memory>



//
struct dgcSubscriptions : public ISubsciptionHandler{
    //
    explicit dgcSubscriptions( std::shared_ptr<database> dbc );
    ~dgcSubscriptions();
    //
    std::vector<boost::mqtt5::subscribe_topic> Subscriptions() const;
    void HandleMessage( std::string sTopic, std::string sMessage );

private:
    std::shared_ptr<database> _dbc;
};
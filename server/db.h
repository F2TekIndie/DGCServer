
#pragma once

#include "config.h"
#include <string>
#include <mutex>



//
namespace dronegroundcontrol {
    class Device;
    class AttributeRequest;
    class TimeseriesRequest;
}
struct sqlite3;
struct Configuration;


//
struct database{
    const std::string sName={"dgc.sqlite"};
    //
    explicit database( Configuration const& config );
    ~database();
    database(const database& rhs) = delete;
    const database& operator=(const database& rhs) = delete;

    //
    void StoreDevice( dronegroundcontrol::Device const& device );
    void StoreAttribute( dronegroundcontrol::AttributeRequest const& request );
    void StoreTimeseries( dronegroundcontrol::TimeseriesRequest const& request );
    
private:
    sqlite3* _db={nullptr};
    std::mutex _mutex;
};

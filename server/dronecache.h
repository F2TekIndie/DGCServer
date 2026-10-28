
#pragma once
#include <memory>
#include <thread>
#include <map>
#include "drone.h"



//
namespace dronegroundcontrol {
    class Device;
    class AttributeRequest;
    class TimeseriesRequest;
}
struct database;
struct Drone;


//
struct CacheData{
    bool _bDirty{false};
    Drone _Drone{};
};

//
struct DroneCache{
    explicit DroneCache( std::shared_ptr<database> dbc );

    //
    void StoreDevice( dronegroundcontrol::Device const& device );
    void StoreAttribute( dronegroundcontrol::AttributeRequest const& request );
    void StoreTimeseries( dronegroundcontrol::TimeseriesRequest const& request );
    //
    bool Exit();

private:
    std::shared_ptr<database>       _db{nullptr};
    std::map< uint32_t, CacheData > _droneCache{};
    //
    std::atomic<bool>               _shouldExit{false};
    std::jthread tDBWorker; 
};

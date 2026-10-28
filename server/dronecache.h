
#pragma once
#include <memory>
#include <thread>
#include <map>
#include <atomic>
#include <mutex>
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
    DroneTimeSeries _TimeSeries{};
    uint64_t _version{0};
};

//
struct DroneCache{
    explicit DroneCache( std::shared_ptr<database> dbc );
    ~DroneCache();

    //
    void StoreDevice( dronegroundcontrol::Device const& device );
    void StoreAttribute( dronegroundcontrol::AttributeRequest const& request );
    void StoreTimeseries( dronegroundcontrol::TimeseriesRequest const& request );
    //
    bool Exit();

private:
    void Synchronize();
    std::mutex _mutex;
    std::shared_ptr<database>       _db{nullptr};
    std::map< uint32_t, CacheData > _droneCache{};
    //
    std::atomic<bool>               _shouldExit{false};
    std::jthread tDBWorker; 
};

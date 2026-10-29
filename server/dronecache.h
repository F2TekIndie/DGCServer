
#pragma once
#include <memory>
#include <thread>
#include <map>
#include <atomic>
#include <mutex>
#include <optional>
#include <set>
#include <condition_variable>
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
    bool _bNew{false};
    Drone _Drone{};
    DroneTimeSeries _TimeSeries{};
    uint64_t _version{0};
};

//
struct DroneCache{
    explicit DroneCache( std::shared_ptr<database> dbc );
    ~DroneCache();

    //
    Drone CreateDevice( uint32_t clientId );
    bool DeleteDevice( dronegroundcontrol::Device const& device );
    void StoreDevice( dronegroundcontrol::Device const& device );
    void StoreAttribute( dronegroundcontrol::AttributeRequest const& request );
    void StoreTimeseries( dronegroundcontrol::TimeseriesRequest const& request );
    //
    std::optional<CacheData> GetDrone( dronegroundcontrol::Device const& device );
    std::vector<Drone> ListDrones( uint32_t clientId );
    bool Exit();

private:
    void Synchronize();
    CacheData& RequireDrone( dronegroundcontrol::Device const& device ); // Caller holds _mutex.
    void Refresh(); // DB worker only: import SQL-only drones.
    std::mutex _mutex;
    std::shared_ptr<database>       _db{nullptr};
    std::map< std::string, CacheData > _droneCache{};
    std::set<std::string> _deletedIds; // Tombstones remain until SQL deletion succeeds.
    std::condition_variable _wakeWorker;
    //
    std::atomic<bool>               _shouldExit{false};
    std::jthread tDBWorker; 
};

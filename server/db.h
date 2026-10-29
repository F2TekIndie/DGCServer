
#pragma once

#include "config.h"
#include "drone.h"
#include <string>
#include <mutex>
#include <utility>



//
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
    void DeleteDrone( std::string const& droneId );
    void StoreDrone( Drone const& drone, DroneTimeSeries& timeseries, bool create = false );
    std::vector<std::pair<Drone, DroneTimeSeries>> LoadDrones( std::string const& droneId = {} );
    std::vector<std::string> LoadDroneIds();
    
private:
    sqlite3* _db={nullptr};
    std::mutex _mutex;
};

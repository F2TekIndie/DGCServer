
#pragma once

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>




//
enum DroneState{
    DS_INIT     = 0,
    DS_OFFLINE  = 1,
    DS_ONLINE   = 2,
    DS_GROUNDED = 3,
    DS_STATIONARY   = 4,
    DS_TRAVELING    = 5
};
//
struct TimeseriesPos{
    std::chrono::sys_time<std::chrono::milliseconds> _timestamp;
    double _posLang;
    double _posLat;
};

struct Drone{
    // Persistet members
    std::string    _droneId;
    uint32_t       _clientId;
    std::string    _name;
    uint32_t       _state;
    bool           _online;
    TimeseriesPos  _position;
    // Non persistet members
    double      _headingRad;
    double      _speedMps;
};

struct DroneTimeSeries{
    std::string    _droneId;
    uint32_t       _clientId;
    // Persistet members
    std::vector< TimeseriesPos > _position;
    std::size_t _persistedPositions{0};
};

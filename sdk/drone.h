
#pragma once

#include <chrono>
#include <cstdint>
#include <vector>
#include <string>




//
struct TimeseriesPos{
    std::chrono::sys_time<std::chrono::milliseconds> _timestamp;
    double _posLang;
    double _posLat;
};

struct Drone{
    // Persistet members
    uint32_t       _droneId;
    uint32_t       _clientId;
    std::string    _name;
    uint32_t       _state;
    bool           _online;
    // Pending history; StoreDrone clears these samples only after commit.
    std::vector< TimeseriesPos > _position;
    // Non persistet members
    double      _headingRad;
    double      _speedMps;
};

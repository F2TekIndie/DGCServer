
#pragma once

#include <chrono>
#include <cstdint>
#include <cstddef>
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
    TimeseriesPos  _position;
    // Non persistet members
    double      _headingRad;
    double      _speedMps;
};

struct DroneTimeSeries{
    uint32_t       _droneId;
    uint32_t       _clientId;
    // Persistet members
    std::vector< TimeseriesPos > _position;
    std::size_t _persistedPositions{0};
};

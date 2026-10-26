
#pragma once
#include <cstdint>
#include <vector>
#include <string>




//
struct DroneManager{
    DroneManager() = default;
    ~DroneManager() = default;
    //
    DroneManager( DroneManager const& rhs ) = delete;
    DroneManager const& operator=( DroneManager const& rhs ) = delete;
    //
    void Init( size_t nCount, bool bCreateNew, bool bRandomValues );    // Id, Name, Online, State, Position
    void Update();              // Online, State, Position
    void UpdateAttributes();    // MQTT <= Online, State, Position
    void UpdateTimeseries();    // MQTT <= Online, State, Position

private:
    std::vector<uint32_t>       _droneIds;
    std::vector<uint32_t>       _clientIds;
    std::vector<std::string>    _names;
    std::vector<uint32_t>       _state;
    std::vector<bool>           _online;
    std::vector<uint32_t>       _posLang;
    std::vector<uint32_t>       _posLat;
};


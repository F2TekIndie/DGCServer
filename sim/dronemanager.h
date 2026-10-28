
#pragma once
#include "mqclient.h"
#include "drone.h"
#include <cstdint>
#include <vector>
#include <string>
#include <random>




struct DroneManager{
    DroneManager( mqClient& mqc );
    ~DroneManager() = default;
    //
    DroneManager( DroneManager const& rhs ) = delete;
    DroneManager const& operator=( DroneManager const& rhs ) = delete;
    //
    void Init( size_t nCount );    // Id, Name, Online, State, Position
    void Update( double dtSeconds );              // Online, State, Position

private:
    void UpdateDevice( Drone const& drone );        // MQTT <= Id, Name, Online, State, Position
    void UpdateAttributes( Drone const& drone );    // MQTT <= Online, State
    void UpdateTimeseries( Drone const& drone );    // MQTT <= Position
    //
    std::vector<Drone> _drones{};
    //
    std::mt19937 _rng{std::random_device{}()};
    std::bernoulli_distribution _onlineChance{0.6};
    std::discrete_distribution<> _states{30, 30, 40 };
    //
    mqClient& _mqttClient;
};


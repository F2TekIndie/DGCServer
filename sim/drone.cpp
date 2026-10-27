
#include "drone.h"
#include "mqtt.h"
#include <algorithm>
#include <iostream>
#include <cassert>
#include <cstdint>
#include <random>
#include <string>




namespace{
    constexpr uint32_t SimulationCliendId = 99;
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
    void PrintDrone( Drone const& drone ){
        std::cout << "** DRONE ** " << std::endl;
        std::cout << "  ID: " << drone._droneId << std::endl;
        std::cout << "  State: " << drone._state << std::endl;
        std::cout << "  Online: " << (drone._online?"True":"False") << std::endl;
        std::cout << "  Pos: " << drone._posLang << " " << drone._posLat << std::endl;
    }
    //
    void CalculateNewDronePosition( Drone& drone, double dtSeconds ){
        constexpr double earthRadiusM = 6371000.0;
        constexpr double radiansPerDegree = std::numbers::pi / 180.0;
        std::mt19937 _rng{std::random_device{}()};

        if (!std::isfinite(dtSeconds) || dtSeconds <= 0.0)
            return;

        std::normal_distribution<double> turnRate{0.0, 0.08}; // rad/s
        drone._headingRad = std::remainder(
            drone._headingRad + turnRate(_rng) * dtSeconds, 2.0 * std::numbers::pi
        );

        const double latitude = drone._posLat * radiansPerDegree;
        const double longitude = drone._posLang * radiansPerDegree;
        const double distance = drone._speedMps * dtSeconds / earthRadiusM;

        // Move along the Earth's surface; heading zero means north.
        const double nextLatitude = std::asin(std::clamp(
            std::sin(latitude) * std::cos(distance) +
            std::cos(latitude) * std::sin(distance) *
                std::cos(drone._headingRad),
            -1.0, 1.0
        ));

        const double nextLongitude = longitude + std::atan2(
            std::sin(drone._headingRad) * std::sin(distance) *
                std::cos(latitude),
            std::cos(distance) -
                std::sin(latitude) * std::sin(nextLatitude)
        );

        drone._posLat = nextLatitude / radiansPerDegree;
        drone._posLang = std::remainder( nextLongitude / radiansPerDegree, 360.0 );
    }
}

DroneManager::DroneManager( mqClient& mqc )
    :_mqttClient(mqc)
{
}

void DroneManager::Init( size_t nCount ){
    assert( nCount > 0);
    // Resize
    _drones.resize( nCount, {0, SimulationCliendId, "Drone_", DS_INIT, false, 10.08, 58.08, 0.0, 5.0 } );
    // Init
    std::uniform_real_distribution<double> heading{ 0.0, 2.0*std::numbers::pi };
    std::uniform_real_distribution<double> speed{ 2.0, 12.0 };
    //
    for ( auto idx{0}; idx < nCount; ++idx ){
        _drones[ idx ]._droneId = 1+idx;
        _drones[ idx ]._name += std::to_string( 1+idx );
        _drones[ idx ]._headingRad = heading( _rng );
        _drones[ idx ]._speedMps = speed( _rng );
        //
        UpdateDevice( _drones[idx] );
    }
}

void DroneManager::Update(double dtSeconds){
    int nLoops = rand() % _drones.size();
    for ( auto idx{0}; idx < nLoops; ++idx ){
        int nRandIndex = rand() % _drones.size();
        // If online
        if ( _onlineChance(_rng) )
        {
            _drones[ nRandIndex ]._online = true;
            // If already ONLINE => Grounded || Stationary || Traveling
            if ( _drones[ nRandIndex ]._state == DS_ONLINE ){
                int n = _states( _rng );
                _drones[ nRandIndex ]._state = DroneState(3 + n);
            }
            else{
                _drones[ nRandIndex ]._state = DS_ONLINE;
            }
        }
        // If offline
        else
        {
            _drones[ nRandIndex ]._online = false;
            _drones[ nRandIndex ]._state = DS_OFFLINE;
        }
        //
        UpdateAttributes( _drones[ nRandIndex ] );
    }
    // All Drones traveling need an update
    std::for_each( _drones.begin(), _drones.end(), [this, dtSeconds](auto& drone){
        // Check if drone is traveling
        if ( drone._state == DS_TRAVELING ){
            // Update gps coordinates
            CalculateNewDronePosition( drone, dtSeconds );
            UpdateTimeseries( drone );
        }
    });
}


void DroneManager::UpdateDevice( Drone const& drone ){
    std::string sMessage;
    _mqttClient.publish( dronesdk::mqtt::EP_DEVICE, sMessage );
}

void DroneManager::UpdateAttributes( Drone const& drone ){
    std::string sMessage;
    _mqttClient.publish( dronesdk::mqtt::EP_ATTRIBUTES, sMessage );
}

void DroneManager::UpdateTimeseries( Drone const& drone ){
    std::string sMessage;
    _mqttClient.publish( dronesdk::mqtt::EP_ATTRIBUTES, sMessage );
}

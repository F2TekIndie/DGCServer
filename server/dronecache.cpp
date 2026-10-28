
#include "dronecache.h"
#include "db.h"
#include "drone.h"
#include "devices.pb.h"
#include "attributes.pb.h"
#include "timeseries.pb.h"
#include <chrono>



//
namespace{
    void UpdateDatabase( std::shared_ptr<database> dbc, Drone& drone ){
        dbc->StoreDrone( drone );
    }
    void DatabaseWorker( DroneCache* pParent ){
        while ( !pParent->Exit() ){
            std::this_thread::sleep_for( std::chrono::milliseconds(100 ) );
        }
    }
}

//
DroneCache::DroneCache( std::shared_ptr<database> dbc )
    :_db( dbc )
{
    tDBWorker = std::jthread( DatabaseWorker, this );
}

//
void DroneCache::StoreDevice( dronegroundcontrol::Device const& device ){

}

void DroneCache::StoreAttribute( dronegroundcontrol::AttributeRequest const& request ){

}

void DroneCache::StoreTimeseries( dronegroundcontrol::TimeseriesRequest const& request ){

}

bool DroneCache::Exit(){
    return _shouldExit;
}
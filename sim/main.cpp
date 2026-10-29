
#include <chrono>
#include <string>
#include "config.h"
#include "mqclient.h"
#include "dronemanager.h"
#include "devices.grpc.pb.h"
#include <grpcpp/grpcpp.h>



namespace{
    std::atomic<bool> _exitSimulator = false;
    void signalExitHandler(int nSig){
        _exitSimulator = true;
    }
    std::vector<std::string> GetAvailableDrones( Configuration const& conf ){
        std::vector<std::string> ids;
        // Discover the registered UUIDs through the server's cache-only RPC.
        auto channel = grpc::CreateChannel(
                std::get<0>(conf._grpcServer) + ":" + std::to_string(std::get<1>(conf._grpcServer)),
                grpc::InsecureChannelCredentials()
            );
        auto service = dronegroundcontrol::DeviceFunctions::NewStub(channel);
        grpc::ClientContext context;
        context.set_deadline( std::chrono::system_clock::now() + std::chrono::seconds(5) );
        dronegroundcontrol::Client client;
        client.set_id(99);
        dronegroundcontrol::Devices registered;
        //
        const auto status = service->ListDevices( &context, client, &registered );
        if ( !status.ok() || registered.devicelist_size() < 100 ){
            std::cerr << "Register at least 100 drones for client 99 and start DGCServer before the simulator.\n";
            return ids;
        }
        //
        for ( auto i = 0; i < registered.devicelist_size(); ++i ){
            ids.push_back( registered.devicelist(i).id() );
        }
        return ids;
    }
}

int main( int argc, char* argv[] ){
    ::signal( SIGINT, signalExitHandler );
    // Read configuration
    Configuration conf = { loadConfiguration("server.ini") };
    // MQTT Client to send drone messages to Broker
    // Either one for all or each drone instance will make it's own connection for stress testing connections
    mqClient mqc( conf );
    mqc.connect();
    mqc.start();
    // Load the available drone ids. We require the db to be pre populated
    auto ids = GetAvailableDrones( conf );
    
    // SIMULATE till shutdown
    DroneManager dm = {mqc};
    {
        // Variables to fill:
        //  ID
        //  Name
        //  State
        //  Online
        //  Position
        dm.Init( ids );

        // Variables to simulate:
        //  Online
        //  State
        //  Position
        auto previousUpdate = std::chrono::steady_clock::now();
        while( !_exitSimulator ){
            std::this_thread::sleep_for( std::chrono::seconds(2) );
            const auto update = std::chrono::steady_clock::now();
            const double dtSeconds =
                std::chrono::duration<double>(update - previousUpdate).count();
            previousUpdate = update;
            dm.Update(dtSeconds);
        }
    }

    // Cleanup
    mqc.disconnect();
    mqc.stop();

    return 0;    
}


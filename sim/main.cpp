
#include <cstdlib>
#include <string>
#include <iostream>
#include "config.h"
#include "mqclient.h"
#include "drone.h"



namespace{
    std::atomic<bool> _exitSimulator = false;
    void signalExitHandler(int nSig){
        _exitSimulator = true;
    }
}

int main( int argc, char* argv[] ){
    ::signal( SIGINT, signalExitHandler );
    // Read commandline input
    // How many Drones to simulate?
    if ( argc < 2 ){
        std::cout << "Invalid number of arguments!" << std::endl;
        return -1;
    }
    int nCount{ std::atoi(argv[1]) };
    if ( nCount < 0 ){
        std::cout << "Negative count for drones!" << std::endl;
        return -1;
    }
    // Read configuration
    Configuration conf = { loadConfiguration("server.ini") };
    // MQTT Client to send drone messages to Broker
    // Either one for all or each drone instance will make it's own connection for stress testing connections
    mqClient mqc( conf );
    mqc.connect();
    mqc.start();
    
    // SIMULATE till shutdown
    DroneManager dm;
    {
        // Variables to fill:
        //  ID
        //  Name
        //  State
        //  Online
        //  Position
        dm.Init( nCount );

        // Variables to simulate:
        //  Online
        //  State
        //  Position
        while( !_exitSimulator ){
            std::this_thread::sleep_for( std::chrono::seconds(1) );
            dm.Update();
        }
    }

    // Cleanup
    mqc.disconnect();
    mqc.stop();

    return 0;    
}


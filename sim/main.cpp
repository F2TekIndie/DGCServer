
#include <iostream>
#include "config.h"
#include "mqclient.h"
#include "drone.h"



int main( int argc, char* argv[] ){
    // Read commandline input
    // How many Drones to simulate?
    // Register Drones first?

    // Read configuration
    Configuration conf = { loadConfiguration("server.ini") };
    // MQTT Client to send drone messages to Broker
    // Either one for all or each drone instance will make it's own connection for stress testing connections
    mqClient mqc( conf );
    mqc.connect();
    
    // SIMULATE till shutdown
    DroneManager dm;
    {
        // Variables to fill:
        //  ID
        //  Name
        //  State
        //  Online
        //  Position

        // Variables to simulate:
        //  Online
        //  State
        //  Position
    }

    // Cleanup
    mqc.disconnect();

    return 0;    
}


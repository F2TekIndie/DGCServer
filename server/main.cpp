
#include <iostream>
#include <memory>
#include <thread>
#include "config.h"
#include "db.h"
#include "grpc.h"
#include "mqclient.h"
#include "mqsubscriptions.h"


namespace{
    std::atomic<bool> _exitServer = false;
    void signalExitHandler(int nSig){
        _exitServer = true;
    }
}

int main( int argc, char* argv[] ){
    ::signal( SIGINT, signalExitHandler );
    // Read configuration
    Configuration conf = { loadConfiguration("server.ini") };
    // DB Connection to store all drone messages and query for DGC
    std::shared_ptr<database> dbc( new database( conf ) );
    // MQTT Client to receive drone messages from Broker
    std::shared_ptr<ISubsciptionHandler> dgcs( new dgcSubscriptions( dbc ) );
    mqClient mqc( conf );
    mqc.connect();
    mqc.subscribe( dgcs );
    mqc.start();
    // gRPC Server to allow DGC to connect to the server
    rpcServer rpcS( conf, dbc );
    bool bSuccess = rpcS.listen();
    if ( bSuccess ){
        rpcS.start();
        // IDLE till shutdown
        while( !_exitServer ){
            std::this_thread::sleep_for( std::chrono::seconds(1) );
        }
        // Cleanup
        rpcS.stop();
    }
    else{
        std::cout << "Could not start gRPC Server!" << std::endl;
    }
    mqc.disconnect();
    mqc.stop();

    return 0;
}

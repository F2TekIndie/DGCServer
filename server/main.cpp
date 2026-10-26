
#include <iostream>
#include <memory>
#include "config.h"
#include "db.h"
#include "grpc.h"
#include "mqclient.h"
#include "mqsubscriptions.h"



int main( int argc, char* argv[] ){
    // Read configuration
    Configuration conf = { loadConfiguration("server.ini") };
    // DB Connection to store all drone messages and query for DGC
    std::shared_ptr<database> dbc( new database( conf ) );
    // MQTT Client to receive drone messages from Broker
    std::shared_ptr<ISubsciptionHandler> dgcs( new dgcSubscriptions( dbc ) );
    mqClient mqc( conf );
    mqc.connect();
    mqc.subscribe( dgcs );
    // gRPC Server to allow DGC to connect to the server
    rpcServer rpcS( conf, dbc );
    rpcS.listen();
    
    // IDLE till shutdown
    ;

    // Cleanup
    rpcS.stop();
    mqc.disconnect();

    return 0;
}

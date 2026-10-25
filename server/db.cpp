
#include "db.h"
#include <sqlite3.h>
#include <iostream>



namespace{
    int callback(void*, int nCount, char** values, char** columns){
        return 0;
    }
}

database::database(){
    int rc = sqlite3_open( sName.c_str(), &_db );
    if( rc ){
        sqlite3_close( _db );
        _db = nullptr;
        std::cerr << "Can not opend database!" << std::endl;
    }
    else{
        char* error = nullptr;
        rc = sqlite3_exec( _db, "PRAGMA foreign_keys = ON", callback, nullptr, &error );
        if ( rc ){
            std::cerr << ( error ? error : sqlite3_errmsg( _db ) ) << std::endl;
            sqlite3_free( error );
        }
    }
}

database::~database(){
    sqlite3_close( _db );
}

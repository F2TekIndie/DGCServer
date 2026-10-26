
#pragma once

#include "config.h"
#include <string>



//
struct sqlite3;
struct Configuration;


//
struct database{
    const std::string sName={"dgc.sqlite"};
    //
    explicit database( Configuration const& config );
    ~database();
    database(const database& rhs) = delete;
    const database& operator=(const database& rhs) = delete;

    //
    ;
    
private:
    sqlite3* _db={nullptr};
};

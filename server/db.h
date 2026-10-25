
#pragma once

#include <string>



//
struct sqlite3;
//
struct database{
    const std::string sName={"dgc.sqlite"};
    //
    database();
    ~database();
    database(const database& rhs) = delete;
    const database& operator=(const database& rhs) = delete;

    //
    ;
    
private:
    sqlite3* _db={nullptr};
};

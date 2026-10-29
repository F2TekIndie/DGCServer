#pragma once

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>



// Drone IDs use canonical lowercase UUID text in Protobuf, the cache and SQLite.
inline bool ValidDroneId( std::string_view id ){
    if ( id.size() != 36 || id == "00000000-0000-0000-0000-000000000000" ) return false;
    for ( std::size_t i = 0; i < id.size(); ++i ){
        if ( i == 8 || i == 13 || i == 18 || i == 23 ){
            if ( id[i] != '-' ) return false;
        }
        else if ( !((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f')) ) return false;
    }
    return true;
}

inline std::string NewDroneId(){
    thread_local boost::uuids::random_generator generator;
    return boost::uuids::to_string( generator() );
}

inline void ValidateDroneName( std::string_view name, bool allowEmpty = false ){
    if ( allowEmpty && name.empty() ) return;
    if ( name.find('\0') != std::string_view::npos || std::all_of(name.begin(), name.end(),
            []( unsigned char c ){ return std::isspace(c); }) ){
        throw std::invalid_argument( "Device name must contain a non-whitespace character and no NUL bytes" );
    }
}

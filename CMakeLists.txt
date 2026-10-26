cmake_minimum_required(VERSION 3.21)

project(DGCServer VERSION 0.1 LANGUAGES CXX)

#
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(THIRD_PARTY_ROOT "/home/f2tek/SwDev" CACHE PATH "Root of third-party libraries")

#
find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../DroneSDK/protocol"
    "${CMAKE_CURRENT_BINARY_DIR}/dronesdkproto"
)

add_executable(dgcserver
    server/main.cpp
    server/db.h server/db.cpp
    server/grpc.h server/grpc.cpp
    server/mqsubscriptions.h server/mqsubscriptions.cpp
    sdk/mqclient.h sdk/mqclient.cpp
    sdk/config.h
)

target_link_libraries(dgcserver PRIVATE
    Boost::system
    Threads::Threads
    DGCProto
    SQLite3::SQLite3
)

target_include_directories(dgcserver PRIVATE
    sdk
    server
)

install(TARGETS dgcserver
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

add_executable(dgcsim
    sim/main.cpp
    sdk/mqclient.h sdk/mqclient.cpp
    sdk/config.h
)

target_link_libraries(dgcsim PRIVATE
    Boost::system
    Threads::Threads
    DGCProto
)

target_include_directories(dgcsim PRIVATE
    sdk
    sim
)

install(TARGETS dgcsim
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

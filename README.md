# DGCServer

**Status: proof-of-concept prototype.** This project is intended for exploration
and testing. Features, APIs, and protocols are incomplete and may change; it is
not production-ready.

Server and simulator scaffolding for Drone Ground Control.

## Current status

The project builds two executables:

| Executable | Intended role | Current behavior |
| --- | --- | --- |
| `dgcserver` | Ground-control server | Exits immediately with status zero. |
| `dgcsim` | Drone simulator | Exits immediately with status zero. |

Both entry points are placeholders. No listening service, MQTT connection,
simulation loop, request processing, or persistence is implemented yet.

## Related repositories

The build expects the shared SDK alongside this repository:

```text
DroneGroundControl/
├── DGC/
├── DGCServer/
└── DroneSDK/
```

- [DGC](../DGC/README.md): Qt desktop application.
- [DroneSDK](../DroneSDK/README.md): shared Protocol Buffer definitions and
  C++ scaffolding, including ASIO and MQTT placeholders.

## Dependencies

- CMake 3.20 or later.
- A C++23 compiler.
- Boost.System and threading support.
- Protocol Buffers compiler and development library.

Qt and GStreamer are not required by the current server build. An MQTT client
library has not yet been selected or linked.

## Build

From this repository:

```sh
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Debug
cmake --build build/local --parallel
```

CMake includes `DroneSDK/protocol` and `DroneSDK/sources`. Protocol Buffer C++
sources are generated during the build. `dgcserver` links `DGCProto`;
`dgcsim` links both `DGCProto` and `DGCSources`.

## Run

```sh
./build/local/dgcserver
./build/local/dgcsim
```

Immediate successful termination is expected at this stage. There are no
implemented command-line options, configured network ports, or broker settings.

## Source layout

```text
server/main.cpp   Server entry point
sim/main.cpp      Simulator entry point
CMakeLists.txt    Build targets and shared SDK dependencies
```

Networking and MQTT source placeholders are maintained in the sibling SDK.

## Shared protocol

Message definitions live in `DroneSDK/protocol`, in the `dronegroundcontrol`
package. Rebuild all consumers after schema changes. Transport framing,
validation, routing, and response/error behavior still need implementation.


#pragma once
#include <string>

/*
    * MQTT ENDPOINTS *
    /device
    /device/attribute
    /device/timeseries
    /status
*/
namespace dronesdk {
    namespace mqtt {
        const std::string EP_DEVICE     = "/device";
        const std::string EP_ATTRIBUTES = "/device/attribute";
        const std::string EP_TIMESERIES = "/device/timeseries";
        const std::string EP_STATUS     = "/status";
    }
}

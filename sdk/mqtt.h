#pragma once
#include <string>



namespace dronesdk {
    namespace mqtt {
        const std::string DEVICE_FILTER     = "/device/+";
        const std::string ATTRIBUTES_FILTER = "/device/+/attributes";
        const std::string TIMESERIES_FILTER = "/device/+/timeseries";
        const std::string EP_STATUS         = "/status";

        inline std::string DeviceTopic( std::string const& id ){
            return "/device/" + id;
        }
        inline std::string AttributesTopic( std::string const& id ){
            return DeviceTopic( id ) + "/attributes";
        }
        inline std::string TimeseriesTopic( std::string const& id ){
            return DeviceTopic( id ) + "/timeseries";
        }
    }
}

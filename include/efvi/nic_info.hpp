#pragma once
#include <string>
#include <cstdint>
#include <array>

namespace efvi {

struct NicInfo {
    std::string           model;           // e.g. "X2522", "X3522"
    std::string           driver_version;
    std::array<uint8_t,6> mac;
    uint32_t              mtu            = 1500;
    uint64_t              port_speed_mbps = 0;
};

} // namespace efvi

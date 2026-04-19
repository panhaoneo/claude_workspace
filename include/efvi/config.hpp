#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace efvi {

enum class LogLevel { DEBUG, INFO, WARN, ERROR };
using LogCallback = std::function<void(LogLevel, const std::string&)>;

enum class NicArch { EF10, EFCT };

enum class FilterType {
    UDP_LOCAL,
    TCP_LOCAL,
    MULTICAST_ALL,
    MULTICAST_IP,
    MAC_VLAN,
};

struct FilterSpec {
    FilterType type     = FilterType::UDP_LOCAL;
    uint32_t   local_ip = 0;    // network byte order
    uint16_t   local_port = 0;  // network byte order
    uint8_t    mac[6]   = {};
    int        vlan_id  = -1;   // -1 = no VLAN
    uint32_t   mcast_ip = 0;    // for MULTICAST_IP

    static FilterSpec udp(uint32_t ip, uint16_t port) {
        FilterSpec s{};
        s.type = FilterType::UDP_LOCAL;
        s.local_ip = ip;
        s.local_port = port;
        return s;
    }

    static FilterSpec tcp(uint32_t ip, uint16_t port) {
        FilterSpec s{};
        s.type = FilterType::TCP_LOCAL;
        s.local_ip = ip;
        s.local_port = port;
        return s;
    }

    static FilterSpec multicast_all() {
        FilterSpec s{};
        s.type = FilterType::MULTICAST_ALL;
        return s;
    }

    static FilterSpec multicast_ip(uint32_t mcast) {
        FilterSpec s{};
        s.type = FilterType::MULTICAST_IP;
        s.mcast_ip = mcast;
        return s;
    }

    static FilterSpec mac_vlan(const uint8_t mac_addr[6], int vlan = -1) {
        FilterSpec s{};
        s.type = FilterType::MAC_VLAN;
        for (int i = 0; i < 6; ++i) s.mac[i] = mac_addr[i];
        s.vlan_id = vlan;
        return s;
    }
};

using FilterCookie = int;

struct PerformanceParams {
    int      rxq_depth       = 512;
    int      txq_depth       = 512;
    int      rx_refill_batch = 32;
    int      ctpio_threshold = 64;
    int      rx_buf_align    = 4 * 1024 * 1024;
    bool     use_hugepages   = false;
    unsigned pd_flags        = 0;  // EF_PD_DEFAULT
};

struct ViConfig {
    std::string           interface;
    PerformanceParams     perf;
    std::vector<FilterSpec> filters;
    LogCallback           log_callback;
};

} // namespace efvi

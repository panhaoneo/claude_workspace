// Software IP/TCP/UDP checksum — required for X3522 (EfCT) TX path
// which does not support hardware TX checksum offload.
#include "checksum.hpp"
#include <cstdint>
#include <cstring>

namespace efvi {
namespace detail {

static uint32_t sum16(const void* data, size_t len, uint32_t acc) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    while (len >= 2) {
        acc += (static_cast<uint32_t>(p[0]) << 8) | p[1];
        p   += 2;
        len -= 2;
    }
    if (len) acc += static_cast<uint32_t>(p[0]) << 8;
    return acc;
}

static uint16_t fold(uint32_t acc) {
    while (acc >> 16) acc = (acc & 0xffff) + (acc >> 16);
    return static_cast<uint16_t>(~acc);
}

uint16_t ip_checksum(const void* iphdr, size_t iphdr_len) {
    return fold(sum16(iphdr, iphdr_len, 0));
}

// Pseudo-header sum for UDP/TCP
static uint32_t pseudo_sum(uint32_t src_ip, uint32_t dst_ip,
                            uint8_t proto, uint16_t payload_len) {
    uint32_t acc = 0;
    acc += (src_ip >> 16) & 0xffff;
    acc += src_ip & 0xffff;
    acc += (dst_ip >> 16) & 0xffff;
    acc += dst_ip & 0xffff;
    acc += proto;
    acc += payload_len;
    return acc;
}

uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip,
                      const void* udphdr, size_t udp_len) {
    uint32_t acc = pseudo_sum(src_ip, dst_ip, 17 /*UDP*/, static_cast<uint16_t>(udp_len));
    acc = sum16(udphdr, udp_len, acc);
    uint16_t result = fold(acc);
    return result ? result : 0xffff;  // UDP checksum 0 means "not computed"
}

uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                      const void* tcphdr, size_t tcp_len) {
    uint32_t acc = pseudo_sum(src_ip, dst_ip, 6 /*TCP*/, static_cast<uint16_t>(tcp_len));
    acc = sum16(tcphdr, tcp_len, acc);
    return fold(acc);
}

} // namespace detail
} // namespace efvi

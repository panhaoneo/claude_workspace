#pragma once
#include <cstddef>
#include <cstdint>

namespace efvi {
namespace detail {

uint16_t ip_checksum(const void* iphdr, size_t iphdr_len);
uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip,
                      const void* udphdr, size_t udp_len);
uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                      const void* tcphdr, size_t tcp_len);

} // namespace detail
} // namespace efvi

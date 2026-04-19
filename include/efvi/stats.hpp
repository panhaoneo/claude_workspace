#pragma once
#include <atomic>
#include <cstdint>

namespace efvi {

struct Stats {
    struct Snapshot {
        uint64_t rx_packets = 0;
        uint64_t tx_packets = 0;
        uint64_t rx_drops   = 0;
        uint64_t tx_drops   = 0;
        uint64_t rx_errors  = 0;
        uint64_t tx_errors  = 0;
        uint64_t latency_ns = 0;

        Snapshot operator+(const Snapshot& o) const {
            Snapshot r;
            r.rx_packets = rx_packets + o.rx_packets;
            r.tx_packets = tx_packets + o.tx_packets;
            r.rx_drops   = rx_drops   + o.rx_drops;
            r.tx_drops   = tx_drops   + o.tx_drops;
            r.rx_errors  = rx_errors  + o.rx_errors;
            r.tx_errors  = tx_errors  + o.tx_errors;
            r.latency_ns = latency_ns + o.latency_ns;
            return r;
        }
    };

    std::atomic<uint64_t> rx_packets{0};
    std::atomic<uint64_t> tx_packets{0};
    std::atomic<uint64_t> rx_drops{0};
    std::atomic<uint64_t> tx_drops{0};
    std::atomic<uint64_t> rx_errors{0};
    std::atomic<uint64_t> tx_errors{0};
    std::atomic<uint64_t> latency_ns{0};

    Snapshot snapshot() const {
        Snapshot s;
        s.rx_packets = rx_packets.load(std::memory_order_relaxed);
        s.tx_packets = tx_packets.load(std::memory_order_relaxed);
        s.rx_drops   = rx_drops.load(std::memory_order_relaxed);
        s.tx_drops   = tx_drops.load(std::memory_order_relaxed);
        s.rx_errors  = rx_errors.load(std::memory_order_relaxed);
        s.tx_errors  = tx_errors.load(std::memory_order_relaxed);
        s.latency_ns = latency_ns.load(std::memory_order_relaxed);
        return s;
    }

    void reset() {
        rx_packets.store(0, std::memory_order_relaxed);
        tx_packets.store(0, std::memory_order_relaxed);
        rx_drops.store(0, std::memory_order_relaxed);
        tx_drops.store(0, std::memory_order_relaxed);
        rx_errors.store(0, std::memory_order_relaxed);
        tx_errors.store(0, std::memory_order_relaxed);
        latency_ns.store(0, std::memory_order_relaxed);
    }
};

struct NicStats {
    uint64_t rx_good_packets = 0;
    uint64_t tx_good_packets = 0;
    uint64_t rx_bad_packets  = 0;
    uint64_t tx_bad_packets  = 0;
    uint64_t rx_multicast    = 0;
};

} // namespace efvi

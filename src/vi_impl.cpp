#include "efvi/vi.hpp"
#include "efvi/config.hpp"
#include "efvi/nic_info.hpp"
#include "efvi/stats.hpp"
#include "efvi/packet.hpp"
#include "efvi/exception.hpp"
#include "checksum.hpp"

#ifdef EFVI_MOCK_MODE
#include "mock/mock_efvi.hpp"
#else
#include <net/if.h>
#include <etherfabric/vi.h>
#include <etherfabric/pd.h>
#include <etherfabric/memreg.h>
#include <etherfabric/pio.h>
#include <etherfabric/efct_vi.h>
#include <etherfabric/vi_set.h>
#include <netinet/in.h>
#endif

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <string>

namespace efvi {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool is_power_of_two(int v) { return v > 0 && (v & (v - 1)) == 0; }

static int refill_batch_valid(int v) {
    return v == 8 || v == 16 || v == 32 || v == 64;
}

// ---------------------------------------------------------------------------
// Vi::Impl
// ---------------------------------------------------------------------------

struct Vi::Impl {
    // --- resource acquisition flags (for RAII cleanup) ---
    bool driver_open_  = false;
    bool pd_alloc_     = false;
    bool vi_alloc_     = false;
    bool pio_alloc_    = false;
    bool memreg_alloc_ = false;

    ef_driver_handle dh_{};
    ef_pd            pd_{};
    ef_vi            vi_{};
    ef_pio           pio_{};
    ef_memreg        memreg_{};

    // RX buffer pool (EF10 only)
    void*  rx_buf_     = nullptr;
    size_t rx_buf_sz_  = 0;
    int    rx_posted_  = 0;
    // ring-slot → physical buf offset (buf_id → slot)
    std::vector<int> rx_free_;  // free buf IDs

    // TX buffer pool (EF10 only, for DMA/PIO paths)
    void*  tx_buf_    = nullptr;
    size_t tx_buf_sz_ = 0;
    static const int TX_BUF_SIZE = 2048;

    // Filter table
    std::map<FilterCookie, ef_filter_cookie> filter_map_;
    int next_cookie_ = 1;
    int efct_filter_count_ = 0;
    static const int EFCT_MAX_FILTERS = 256;

    // Metadata
    NicArch  arch_{NicArch::EF10};
    NicInfo  nic_info_{};
    Stats    stats_{};
    ViConfig cfg_{};

    // ---------------------------------------------------------------------------
    ~Impl() {
        // Remove all filters
        for (auto& kv : filter_map_)
            ef_vi_filter_del(&vi_, dh_, &kv.second);
        filter_map_.clear();

        if (tx_buf_) { free(tx_buf_); tx_buf_ = nullptr; }
        if (rx_buf_) { free(rx_buf_); rx_buf_ = nullptr; }

        if (pio_alloc_)    ef_pio_free(&pio_, dh_);
        if (memreg_alloc_) ef_memreg_free(&memreg_, dh_);
        if (vi_alloc_)     ef_vi_free(&vi_, dh_);
        if (pd_alloc_)     ef_pd_free(&pd_, dh_);
        if (driver_open_)  ef_driver_close(dh_);
    }

    // ---------------------------------------------------------------------------
    void log(LogLevel lvl, const std::string& msg) const {
        if (cfg_.log_callback) cfg_.log_callback(lvl, msg);
    }

    // ---------------------------------------------------------------------------
    void init(const ViConfig& cfg) {
        cfg_ = cfg;

        // Validate config
        if (cfg.interface.empty())
            throw ViException(EINVAL, "interface name is empty");

        if (!is_power_of_two(cfg.perf.rxq_depth) ||
            cfg.perf.rxq_depth < 64 || cfg.perf.rxq_depth > 4096)
            throw ViException(EINVAL, "rxq_depth must be a power-of-2 in [64,4096]");

        if (!is_power_of_two(cfg.perf.txq_depth) ||
            cfg.perf.txq_depth < 64 || cfg.perf.txq_depth > 4096)
            throw ViException(EINVAL, "txq_depth must be a power-of-2 in [64,4096]");

        if (!refill_batch_valid(cfg.perf.rx_refill_batch))
            throw ViException(EINVAL, "rx_refill_batch must be 8/16/32/64");

        if (cfg.perf.ctpio_threshold < 0 || cfg.perf.ctpio_threshold > 1500)
            throw ViException(EINVAL, "ctpio_threshold must be in [0,1500]");

        // Open driver
        log(LogLevel::INFO, "opening ef_vi driver");
        int rc = ef_driver_open(&dh_);
        if (rc < 0)
            throw ViException(-rc, "ef_driver_open failed");
        driver_open_ = true;

        // Resolve interface index
        int ifindex = 0;
#ifndef EFVI_MOCK_MODE
        ifindex = static_cast<int>(if_nametoindex(cfg.interface.c_str()));
        if (ifindex == 0) {
            throw ViException(errno,
                "interface not found: " + cfg.interface);
        }
#endif

        // Allocate Protection Domain
        log(LogLevel::INFO, "allocating PD on " + cfg.interface);
        rc = ef_pd_alloc(&pd_, dh_, ifindex, cfg.perf.pd_flags);
        if (rc < 0)
            throw ViException(-rc, "ef_pd_alloc failed");
        pd_alloc_ = true;

        // Allocate VI from PD
        log(LogLevel::INFO, "allocating VI");
        rc = ef_vi_alloc_from_pd(&vi_, dh_, &pd_, dh_,
                                  -1,                    // evq embedded
                                  cfg.perf.rxq_depth,
                                  cfg.perf.txq_depth,
                                  nullptr, 0, EF_VI_FLAGS_DEFAULT);
        if (rc < 0)
            throw ViException(-rc, "ef_vi_alloc_from_pd failed");
        vi_alloc_ = true;

        // Detect NIC architecture
        const ef_nic_type* nt = ef_vi_nic_type(&vi_);
        if (nt->arch == EF_VI_ARCH_EFCT)
            arch_ = NicArch::EFCT;
        else
            arch_ = NicArch::EF10;

        // EfCT does not support PHYS_MODE
        if (arch_ == NicArch::EFCT && (cfg.perf.pd_flags & EF_PD_PHYS_MODE))
            throw ViException(EINVAL,
                "EF_PD_PHYS_MODE is not supported on X3522/EfCT");

        // EfCT mandates hugepages
        if (arch_ == NicArch::EFCT)
            cfg_.perf.use_hugepages = true;

        // Populate NIC info
        populate_nic_info(nt);

        // EF10: allocate RX/TX buffer pool, register with DMA, post descriptors
        if (arch_ == NicArch::EF10) {
            setup_rx_buffers();
            setup_tx_buffers();
            try_alloc_pio();
        }

        // Install initial filters
        for (const auto& fs : cfg.filters)
            add_filter_internal(fs);

        log(LogLevel::INFO, "Vi ready on " + cfg.interface +
            " arch=" + (arch_ == NicArch::EF10 ? "EF10" : "EfCT"));
    }

    // ---------------------------------------------------------------------------
    void populate_nic_info(const ef_nic_type* nt) {
        if (nt->arch == EF_VI_ARCH_EFCT) {
            nic_info_.model          = "X3522";
            nic_info_.port_speed_mbps = 100000;
        } else {
            nic_info_.model          = "X2522";
            nic_info_.port_speed_mbps = 10000;
        }
        nic_info_.driver_version = "OpenOnload-8.1.26";
        nic_info_.mtu            = 1500;
        nic_info_.mac            = {};
    }

    // ---------------------------------------------------------------------------
    void setup_rx_buffers() {
        const int n_bufs   = cfg_.perf.rxq_depth;
        const size_t stride = 2048;
        rx_buf_sz_         = static_cast<size_t>(n_bufs) * stride;

        // Allocate aligned memory
        if (posix_memalign(&rx_buf_, 4096, rx_buf_sz_) != 0)
            throw ViException(ENOMEM, "posix_memalign for RX buffers failed");
        memset(rx_buf_, 0, rx_buf_sz_);

        int rc = ef_memreg_alloc(&memreg_, dh_, &pd_, dh_, rx_buf_, rx_buf_sz_);
        if (rc < 0) {
            free(rx_buf_); rx_buf_ = nullptr;
            throw ViException(-rc, "ef_memreg_alloc failed");
        }
        memreg_alloc_ = true;

        // Post all descriptors into the RX ring
        for (int i = 0; i < n_bufs; ++i) {
            ef_addr dma = ef_memreg_dma_addr(&memreg_,
                                              static_cast<size_t>(i) * stride);
            ef_vi_receive_post(&vi_, dma, i);
        }
        rx_posted_ = n_bufs;

        // Prepare free-list for refill
        rx_free_.clear();
    }

    // ---------------------------------------------------------------------------
    void setup_tx_buffers() {
        const int n_bufs  = cfg_.perf.txq_depth;
        tx_buf_sz_        = static_cast<size_t>(n_bufs) * TX_BUF_SIZE;

        if (posix_memalign(&tx_buf_, 4096, tx_buf_sz_) != 0)
            throw ViException(ENOMEM, "posix_memalign for TX buffers failed");
        memset(tx_buf_, 0, tx_buf_sz_);
    }

    // ---------------------------------------------------------------------------
    void try_alloc_pio() {
        // PIO is optional on EF10; silently skip if not available
        int rc = ef_pio_alloc(&pio_, dh_, &pd_, dh_, 0);
        if (rc < 0) {
            log(LogLevel::INFO, "PIO not available, falling back to DMA TX");
            return;
        }
        pio_alloc_ = true;
        rc = ef_pio_link_vi(&pio_, dh_, &vi_, dh_);
        if (rc < 0) {
            log(LogLevel::WARN, "ef_pio_link_vi failed, PIO disabled");
            ef_pio_free(&pio_, dh_);
            pio_alloc_ = false;
        }
    }

    // ---------------------------------------------------------------------------
    void refill_rx_ring() {
        if (arch_ != NicArch::EF10) return;
        const size_t stride = 2048;
        int low = cfg_.perf.rxq_depth / 4;
        while (rx_posted_ < cfg_.perf.rxq_depth - low && !rx_free_.empty()) {
            int id = rx_free_.back(); rx_free_.pop_back();
            ef_addr dma = ef_memreg_dma_addr(&memreg_,
                                              static_cast<size_t>(id) * stride);
            ef_vi_receive_post(&vi_, dma, id);
            ++rx_posted_;
        }
    }

    // ---------------------------------------------------------------------------
    void send(const void* buf, size_t len) {
        // Hot path — no logging, no malloc
        if (arch_ == NicArch::EFCT) {
            // EfCT: CTPIO only (store-and-forward auto fallback in HW)
            // For EfCT we must compute checksums in software.
            // Caller is expected to have pre-set them, or we do a best-effort.
            ef_vi_transmit_ctpio_copy(&vi_, buf, len,
                                      cfg_.perf.ctpio_threshold);
            ef_vi_transmit_push(&vi_);
        } else {
            // EF10: prefer CTPIO, fall back to DMA
            int rc = ef_vi_transmit_ctpio_copy(&vi_, buf, len,
                                               cfg_.perf.ctpio_threshold);
            if (rc != 0) {
                // DMA fallback: copy into TX buffer slot 0 (simplified)
                if (len <= static_cast<size_t>(TX_BUF_SIZE) && tx_buf_) {
                    memcpy(tx_buf_, buf, len);
                    ef_addr dma = ef_memreg_dma_addr(&memreg_, 0);
                    ef_vi_transmit(&vi_, dma, static_cast<int>(len), 0);
                } else {
                    stats_.tx_drops.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            } else {
                ef_vi_transmit_push(&vi_);
            }
        }
        stats_.tx_packets.fetch_add(1, std::memory_order_relaxed);
    }

    // ---------------------------------------------------------------------------
    void send_batch(const void* const* bufs, const size_t* lens, int count) {
        // EF10 only
        for (int i = 0; i < count; ++i)
            send(bufs[i], lens[i]);
    }

    // ---------------------------------------------------------------------------
    // Returns number of packets placed into batch (no malloc on hot path —
    // batch.packets_ uses reserved capacity set by caller).
    int poll(RxBatch& batch) {
        batch.clear();

        ef_event evs[64];
        int n = ef_eventq_poll(&vi_, evs,
                               static_cast<int>(sizeof(evs) / sizeof(evs[0])));
        if (n <= 0) return 0;

        for (int i = 0; i < n; ++i) {
            int type = EF_EVENT_TYPE(evs[i]);

            if (type == EF_EVENT_TYPE_RX) {
                int   buf_id = EF_EVENT_RX_DESC_ID(evs[i]);
                size_t pkt_len = EF_EVENT_RX_BYTES(evs[i]);
                const void* ptr = nullptr;
                if (rx_buf_)
                    ptr = static_cast<char*>(rx_buf_) +
                          static_cast<size_t>(buf_id) * 2048;

                // Get hardware timestamp (best effort)
                ef_timespec ts{};
                unsigned ts_flags = 0;
                ef_vi_receive_get_timestamp_sync(&vi_, ptr, &ts, &ts_flags);
                uint64_t ts_ns = ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;

                // EF10 packet: no release callback needed
                batch.push_packet(PacketRef(nullptr, nullptr, -1,
                                            ptr, pkt_len, ts_ns));

                // Return descriptor to free list for refill
                rx_free_.push_back(buf_id);
                --rx_posted_;
                stats_.rx_packets.fetch_add(1, std::memory_order_relaxed);

            } else if (type == EF_EVENT_TYPE_RX_REF) {
                // EfCT packet
                int pkt_id   = EF_EVENT_RX_REF_PKT_ID(evs[i]);
                size_t pkt_len = EF_EVENT_RX_REF_BYTES(evs[i]);
                uint64_t ts_ns = EF_EVENT_RX_REF_TIMESTAMP(evs[i]);

                const void* ptr = nullptr;
                efct_vi_rxpkt_get(&vi_, pkt_id, &ptr);

                auto release = [](void* ctx, int id) {
                    efct_vi_rxpkt_release(static_cast<ef_vi*>(ctx), id);
                };
                batch.push_packet(PacketRef(release, &vi_, pkt_id,
                                            ptr, pkt_len, ts_ns));
                stats_.rx_packets.fetch_add(1, std::memory_order_relaxed);

            } else if (type == EF_EVENT_TYPE_TX) {
                // TX completion — descriptor recycled (nothing to do)

            } else if (type == EF_EVENT_TYPE_RX_NO_DESC_TRUNC ||
                       type == EF_EVENT_TYPE_RX_DISCARD) {
                stats_.rx_drops.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Refill RX ring (EF10 only)
        refill_rx_ring();

        return static_cast<int>(batch.size());
    }

    // ---------------------------------------------------------------------------
    FilterCookie add_filter_internal(const FilterSpec& spec) {
        ef_filter_spec fs;
        ef_filter_spec_init(&fs, EF_FILTER_FLAG_NONE);

        // EfCT: enforce filter count limit (256 per port)
        if (arch_ == NicArch::EFCT &&
            efct_filter_count_ >= EFCT_MAX_FILTERS) {
            throw ViException(ENOSPC,
                "X3522 filter limit (256) reached");
        }

        int rc = 0;
        switch (spec.type) {
        case FilterType::UDP_LOCAL:
            rc = ef_filter_spec_set_ip4_local(&fs, IPPROTO_UDP,
                                              spec.local_ip,
                                              static_cast<int>(spec.local_port));
            break;
        case FilterType::TCP_LOCAL:
            rc = ef_filter_spec_set_ip4_local(&fs, IPPROTO_TCP,
                                              spec.local_ip,
                                              static_cast<int>(spec.local_port));
            break;
        case FilterType::MULTICAST_ALL:
            rc = ef_filter_spec_set_multicast_all(&fs);
            break;
        case FilterType::MULTICAST_IP:
            rc = ef_filter_spec_set_ip4_local(&fs, IPPROTO_UDP,
                                              spec.mcast_ip, 0);
            break;
        case FilterType::MAC_VLAN:
            rc = ef_filter_spec_set_eth_local(&fs, spec.vlan_id, spec.mac);
            break;
        }
        if (rc < 0)
            throw ViException(-rc, "ef_filter_spec_set failed");

        ef_filter_cookie ck{};
        rc = ef_vi_filter_add(&vi_, dh_, &fs, &ck);
        if (rc < 0)
            throw ViException(-rc, "ef_vi_filter_add failed");

        FilterCookie cookie = next_cookie_++;
        filter_map_[cookie] = ck;
        if (arch_ == NicArch::EFCT) ++efct_filter_count_;

        log(LogLevel::INFO, "filter added cookie=" + std::to_string(cookie));
        return cookie;
    }

    // ---------------------------------------------------------------------------
    FilterCookie add_filter(const FilterSpec& spec) {
        return add_filter_internal(spec);
    }

    void remove_filter(FilterCookie cookie) {
        auto it = filter_map_.find(cookie);
        if (it == filter_map_.end())
            throw ViException(ENOENT,
                "unknown filter cookie " + std::to_string(cookie));

        int rc = ef_vi_filter_del(&vi_, dh_, &it->second);
        if (rc < 0)
            log(LogLevel::WARN, "ef_vi_filter_del failed: " + std::to_string(rc));

        if (arch_ == NicArch::EFCT && efct_filter_count_ > 0)
            --efct_filter_count_;

        filter_map_.erase(it);
        log(LogLevel::INFO, "filter removed cookie=" + std::to_string(cookie));
    }
};

// ---------------------------------------------------------------------------
// Vi public API
// ---------------------------------------------------------------------------

Vi::Vi(const ViConfig& cfg) : impl_(new Impl()) {
    impl_->init(cfg);
}

Vi::~Vi() = default;

Vi::Vi(Vi&& o) noexcept : impl_(std::move(o.impl_)) {}

Vi& Vi::operator=(Vi&& o) noexcept {
    impl_ = std::move(o.impl_);
    return *this;
}

void Vi::send(const void* buf, size_t len)                            { impl_->send(buf, len); }
void Vi::send_batch(const void* const* bufs, const size_t* lens, int count) { impl_->send_batch(bufs, lens, count); }
int  Vi::poll(RxBatch& batch)                                         { return impl_->poll(batch); }

FilterCookie Vi::add_filter(const FilterSpec& spec)    { return impl_->add_filter(spec); }
void         Vi::remove_filter(FilterCookie cookie)    { impl_->remove_filter(cookie); }

NicArch          Vi::arch()           const { return impl_->arch_; }
const NicInfo&   Vi::nic_info()       const { return impl_->nic_info_; }
Stats::Snapshot  Vi::stats_snapshot() const { return impl_->stats_.snapshot(); }
void             Vi::reset_stats()          { impl_->stats_.reset(); }

} // namespace efvi

#pragma once
#include "config.hpp"
#include "nic_info.hpp"
#include "stats.hpp"
#include "packet.hpp"
#include "exception.hpp"
#include <memory>

namespace efvi {

class Vi {
public:
    explicit Vi(const ViConfig& cfg);
    ~Vi();

    Vi(const Vi&) = delete;
    Vi& operator=(const Vi&) = delete;
    Vi(Vi&&) noexcept;
    Vi& operator=(Vi&&) noexcept;

    // TX — contiguous buffer only; no scatter-gather
    void send(const void* buf, size_t len);

    // X2522 only: batch send to reduce doorbell count
    void send_batch(const void* const* bufs, const size_t* lens, int count);

    // RX — returns number of packets placed into batch
    int poll(RxBatch& batch);

    // Filters
    FilterCookie add_filter(const FilterSpec& spec);
    void         remove_filter(FilterCookie cookie);

    // Metadata
    NicArch           arch()           const;
    const NicInfo&    nic_info()       const;
    Stats::Snapshot   stats_snapshot() const;
    void              reset_stats();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace efvi

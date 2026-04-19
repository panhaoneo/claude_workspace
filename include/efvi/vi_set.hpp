#pragma once
#include "vi.hpp"
#include "config.hpp"
#include "stats.hpp"
#include <vector>
#include <memory>

namespace efvi {

struct ViSetConfig {
    int                   queue_count = 1;
    ViConfig              base_cfg;          // shared defaults for each queue
    std::vector<ViConfig> per_queue_cfg;     // overrides per queue (optional)
};

class ViSet {
public:
    explicit ViSet(const ViSetConfig& cfg);
    ~ViSet();

    ViSet(const ViSet&) = delete;
    ViSet& operator=(const ViSet&) = delete;
    ViSet(ViSet&&) noexcept;
    ViSet& operator=(ViSet&&) noexcept;

    Vi& vi(int queue_idx);
    int queue_count() const;

    Stats::Snapshot aggregate_stats() const;
    void            reset_all_stats();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace efvi

#include "efvi/vi_set.hpp"
#include "efvi/exception.hpp"

#ifdef EFVI_MOCK_MODE
#include "mock/mock_efvi.hpp"
#else
#include <net/if.h>
#include <etherfabric/vi.h>
#include <etherfabric/pd.h>
#include <etherfabric/vi_set.h>
#endif

#include <cassert>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace efvi {

struct ViSet::Impl {
    std::vector<Vi> queues_;

    ~Impl() = default;

    void init(const ViSetConfig& cfg) {
        if (cfg.queue_count < 1)
            throw ViException(EINVAL, "queue_count must be >= 1");

        queues_.reserve(static_cast<size_t>(cfg.queue_count));

        for (int i = 0; i < cfg.queue_count; ++i) {
            // Per-queue config overrides base if provided
            ViConfig qcfg = cfg.base_cfg;
            if (i < static_cast<int>(cfg.per_queue_cfg.size()))
                qcfg = cfg.per_queue_cfg[static_cast<size_t>(i)];

            queues_.emplace_back(qcfg);
        }
    }
};

ViSet::ViSet(const ViSetConfig& cfg) : impl_(new Impl()) {
    impl_->init(cfg);
}

ViSet::~ViSet() = default;

ViSet::ViSet(ViSet&& o) noexcept : impl_(std::move(o.impl_)) {}

ViSet& ViSet::operator=(ViSet&& o) noexcept {
    impl_ = std::move(o.impl_);
    return *this;
}

Vi& ViSet::vi(int queue_idx) {
    if (queue_idx < 0 || queue_idx >= static_cast<int>(impl_->queues_.size()))
        throw ViException(ERANGE,
            "queue_idx out of range: " + std::to_string(queue_idx));
    return impl_->queues_[static_cast<size_t>(queue_idx)];
}

int ViSet::queue_count() const {
    return static_cast<int>(impl_->queues_.size());
}

Stats::Snapshot ViSet::aggregate_stats() const {
    Stats::Snapshot total{};
    for (const auto& v : impl_->queues_)
        total = total + v.stats_snapshot();
    return total;
}

void ViSet::reset_all_stats() {
    for (auto& v : impl_->queues_)
        v.reset_stats();
}

} // namespace efvi

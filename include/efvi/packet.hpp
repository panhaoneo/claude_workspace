#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace efvi {

// RAII wrapper for EfCT (X3522) received packet references.
// For EF10 (X2522) packets, data() points directly into the RX ring buffer.
class PacketRef {
public:
    using ReleaseFn = void(*)(void* ctx, int pkt_id);

    ~PacketRef() {
        if (release_fn_ && pkt_id_ >= 0)
            release_fn_(ctx_, pkt_id_);
    }

    PacketRef(PacketRef&& o) noexcept
        : release_fn_(o.release_fn_), ctx_(o.ctx_), pkt_id_(o.pkt_id_),
          data_(o.data_), len_(o.len_), timestamp_ns_(o.timestamp_ns_) {
        o.pkt_id_ = -1;
    }

    PacketRef& operator=(PacketRef&& o) noexcept {
        if (this != &o) {
            if (release_fn_ && pkt_id_ >= 0) release_fn_(ctx_, pkt_id_);
            release_fn_  = o.release_fn_;
            ctx_         = o.ctx_;
            pkt_id_      = o.pkt_id_;
            data_        = o.data_;
            len_         = o.len_;
            timestamp_ns_= o.timestamp_ns_;
            o.pkt_id_ = -1;
        }
        return *this;
    }

    PacketRef(const PacketRef&) = delete;
    PacketRef& operator=(const PacketRef&) = delete;

    const void* data()         const { return data_; }
    size_t      len()          const { return len_; }
    uint64_t    timestamp_ns() const { return timestamp_ns_; }

    // Internal constructor — not part of the public API.
    // PacketRef objects are produced exclusively by Vi::poll() via RxBatch.
    PacketRef(ReleaseFn fn, void* ctx, int pkt_id,
              const void* data, size_t len, uint64_t ts)
        : release_fn_(fn), ctx_(ctx), pkt_id_(pkt_id),
          data_(data), len_(len), timestamp_ns_(ts) {}

private:
    ReleaseFn   release_fn_  = nullptr;
    void*       ctx_         = nullptr;
    int         pkt_id_      = -1;
    const void* data_        = nullptr;
    size_t      len_         = 0;
    uint64_t    timestamp_ns_= 0;
};

class RxBatch {
public:
    RxBatch()  = default;
    ~RxBatch() = default;

    RxBatch(const RxBatch&) = delete;
    RxBatch& operator=(const RxBatch&) = delete;
    RxBatch(RxBatch&&) = default;
    RxBatch& operator=(RxBatch&&) = default;

    void   clear()            { packets_.clear(); }
    bool   empty()      const { return packets_.empty(); }
    size_t size()       const { return packets_.size(); }

    PacketRef&       operator[](size_t i)       { return packets_[i]; }
    const PacketRef& operator[](size_t i) const { return packets_[i]; }

    std::vector<PacketRef>::iterator begin()             { return packets_.begin(); }
    std::vector<PacketRef>::iterator end()               { return packets_.end(); }
    std::vector<PacketRef>::const_iterator begin() const { return packets_.begin(); }
    std::vector<PacketRef>::const_iterator end()   const { return packets_.end(); }

    // Internal: called only from Vi::Impl::poll() to append a received packet.
    void push_packet(PacketRef&& ref) { packets_.push_back(std::move(ref)); }

private:
    std::vector<PacketRef> packets_;
};

} // namespace efvi

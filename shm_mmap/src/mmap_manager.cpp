#include "mmap_manager.hpp"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>

#include "file_header.hpp"
#include "market_records.hpp"
#include "mmap_file.hpp"
#include "tick_file_pool.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// 内部工具
// ─────────────────────────────────────────────────────────────────────────────

namespace {

bool mkdir_p(const std::string& path) {
    size_t pos = 1;
    while ((pos = path.find('/', pos)) != std::string::npos) {
        std::string sub = path.substr(0, pos);
        if (::mkdir(sub.c_str(), 0755) != 0 && errno != EEXIST) return false;
        ++pos;
    }
    return ::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 路径构建
// ─────────────────────────────────────────────────────────────────────────────

/*static*/ std::string MmapManager::exchange_str(uint8_t exchange) {
    return exchange == 0 ? "sh" : "sz";
}

std::string MmapManager::snapshot_path(const char* symbol,
                                        uint8_t     exchange) const {
    return cfg_.base_dir + "/snapshot/" + exchange_str(exchange)
         + "/" + cfg_.date + "/" + symbol + ".mmap";
}

std::string MmapManager::tick_path(uint32_t channel, uint8_t exchange,
                                    uint32_t file_seq) const {
    char buf[64];
    ::snprintf(buf, sizeof(buf), "/ch_%04u_%04u.mmap", channel, file_seq);
    return cfg_.base_dir + "/tick/" + exchange_str(exchange)
         + "/" + cfg_.date + buf;
}

std::string MmapManager::pool_dir_path() const {
    return cfg_.base_dir + "/tick/pool";
}

// ─────────────────────────────────────────────────────────────────────────────
// 构造（写端）
// ─────────────────────────────────────────────────────────────────────────────

MmapManager::MmapManager(const Config& cfg) : cfg_(cfg) {
    // 创建目录树
    mkdir_p(cfg_.base_dir + "/snapshot/sh/" + cfg_.date);
    mkdir_p(cfg_.base_dir + "/snapshot/sz/" + cfg_.date);
    mkdir_p(cfg_.base_dir + "/tick/sh/" + cfg_.date);
    mkdir_p(cfg_.base_dir + "/tick/sz/" + cfg_.date);
    mkdir_p(pool_dir_path());

    // 初始化并预分配逐笔文件池
    tick_pool_ = std::make_unique<TickFilePool>(
        pool_dir_path(),
        sizeof(TickRecord),
        cfg_.tick_capacity_per_file,
        cfg_.tick_total_preallocated);
    tick_pool_->preallocate();
}

// ─────────────────────────────────────────────────────────────────────────────
// 只读工厂（读端）
// ─────────────────────────────────────────────────────────────────────────────

/*static*/ std::unique_ptr<MmapManager>
MmapManager::open_readonly(const std::string& base_dir,
                            const std::string& date)
{
    Config cfg;
    cfg.base_dir = base_dir;
    cfg.date     = date;

    // 通过私有构造（不触发池预分配）创建只读实例
    auto mgr = std::unique_ptr<MmapManager>(new MmapManager());
    mgr->cfg_      = cfg;
    mgr->readonly_ = true;
    return mgr;
}

MmapManager::~MmapManager() {
    flush_all();
}

// ─────────────────────────────────────────────────────────────────────────────
// 写端：快照
// ─────────────────────────────────────────────────────────────────────────────

int64_t MmapManager::append_snapshot(const char*           symbol,
                                      uint8_t               exchange,
                                      const SnapshotRecord& rec)
{
    // 懒创建：首次收到该 symbol 数据时创建文件
    auto& file_ptr = snapshot_files_[symbol];
    if (!file_ptr) {
        std::string path = snapshot_path(symbol, exchange);
        file_ptr = MmapFile::create_snapshot(path, cfg_.snapshot_capacity,
                                              symbol, exchange);
        if (!file_ptr) return -1;
    }

    int64_t idx = file_ptr->append_snapshot(rec);
    if (idx < 0) {
        snapshot_drops_.fetch_add(1, std::memory_order_relaxed);
    }
    return idx;
}

// ─────────────────────────────────────────────────────────────────────────────
// 写端：逐笔
// ─────────────────────────────────────────────────────────────────────────────

MmapManager::ChannelCtx& MmapManager::get_or_create_channel(uint32_t channel) {
    return tick_channels_[channel];  // unordered_map 首次访问时默认构造
}

std::unique_ptr<MmapFile>
MmapManager::allocate_tick_file(uint32_t channel, uint8_t exchange,
                                 uint32_t file_seq, uint64_t seq_start)
{
    // 确保目标目录存在（正式路径的父目录）
    std::string dir = cfg_.base_dir + "/tick/" + exchange_str(exchange)
                    + "/" + cfg_.date;
    mkdir_p(dir);

    std::string path = tick_path(channel, exchange, file_seq);
    std::unique_ptr<MmapFile> new_file;
    if (!tick_pool_->acquire(path, &new_file)) return nullptr;

    // 更新 FileHeader 中的 tick 专用字段（pool 文件头中这些字段为 0）
    FileHeader* hdr = new_file->header();
    hdr->channel   = channel;
    hdr->file_seq  = static_cast<uint16_t>(file_seq);
    hdr->exchange  = exchange;
    hdr->seq_start = seq_start;
    hdr->seq_end   = 0;

    return new_file;
}

MmapFile* MmapManager::ensure_tick_file(ChannelCtx& ctx,
                                          uint32_t    channel,
                                          uint8_t     exchange,
                                          uint64_t    seq)
{
    uint64_t file_idx = (seq - 1) / cfg_.tick_capacity_per_file;

    // 快速路径：已密封文件
    if (file_idx < ctx.sealed_files.size()) {
        return ctx.sealed_files[file_idx].get();
    }

    // 快速路径：active 文件已是目标文件
    if (ctx.active_file && ctx.sealed_files.size() == file_idx) {
        return ctx.active_file.get();
    }

    // 需要创建/推进到 file_idx
    // 步骤 1：若 active_file 落后于 file_idx，先密封它
    if (ctx.active_file && ctx.sealed_files.size() < file_idx) {
        ctx.active_file->header()->seq_end =
            ctx.active_file->header()->seq_start
            + cfg_.tick_capacity_per_file - 1;
        ctx.active_file->msync_header();
        ctx.sealed_files.push_back(std::move(ctx.active_file));
        // ctx.active_file 现在为 null
    }

    // 步骤 2：为跨文件 gap 的中间段创建占位密封文件
    // 循环不变量：sealed_files.size() < file_idx，active_file == null
    while (ctx.sealed_files.size() < file_idx) {
        uint32_t fseq   = static_cast<uint32_t>(ctx.sealed_files.size());
        uint64_t sstart = static_cast<uint64_t>(fseq)
                        * cfg_.tick_capacity_per_file + 1;
        auto f = allocate_tick_file(channel, exchange, fseq, sstart);
        if (!f) return nullptr;
        f->header()->seq_end = sstart + cfg_.tick_capacity_per_file - 1;
        ctx.sealed_files.push_back(std::move(f));
    }

    // 步骤 3：现在 sealed_files.size() == file_idx，active_file == null，
    //         创建目标 active 文件
    uint32_t new_fseq  = static_cast<uint32_t>(file_idx);
    uint64_t seq_start = static_cast<uint64_t>(new_fseq)
                       * cfg_.tick_capacity_per_file + 1;
    ctx.active_file = allocate_tick_file(channel, exchange, new_fseq, seq_start);
    if (!ctx.active_file) return nullptr;
    return ctx.active_file.get();
}

bool MmapManager::append_tick(uint32_t          channel,
                               uint8_t           exchange,
                               uint64_t          seq,
                               const TickRecord& rec)
{
    ChannelCtx& ctx = get_or_create_channel(channel);

    uint64_t slot_idx = (seq - 1) % cfg_.tick_capacity_per_file;

    // slot_idx == 0 表示新文件的第一条：密封当前 active，从池取新文件
    if (slot_idx == 0 && ctx.active_file) {
        // 设置末 seq
        ctx.active_file->header()->seq_end =
            ctx.active_file->header()->seq_start
            + cfg_.tick_capacity_per_file - 1;
        ctx.active_file->msync_header();
        ctx.sealed_files.push_back(std::move(ctx.active_file));
    }

    MmapFile* f = ensure_tick_file(ctx, channel, exchange, seq);
    if (!f) return false;  // 池耗尽

    return f->write_tick(slot_idx, rec);
}

bool MmapManager::mark_gap(uint32_t channel, uint8_t exchange, uint64_t seq) {
    ChannelCtx& ctx = get_or_create_channel(channel);
    MmapFile* f = ensure_tick_file(ctx, channel, exchange, seq);
    if (!f) return false;

    uint64_t slot_idx = (seq - 1) % cfg_.tick_capacity_per_file;
    TickRecord* slot = static_cast<TickRecord*>(f->at_mutable(slot_idx));
    if (!slot) return false;

    slot->state.store(static_cast<uint8_t>(SlotState::kGap),
                      std::memory_order_release);
    f->header()->gap_count.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool MmapManager::recover_tick(uint32_t          channel,
                                uint8_t           exchange,
                                uint64_t          seq,
                                const TickRecord& rec)
{
    ChannelCtx& ctx = get_or_create_channel(channel);

    uint64_t file_idx = (seq - 1) / cfg_.tick_capacity_per_file;
    uint64_t slot_idx = (seq - 1) % cfg_.tick_capacity_per_file;

    // 定位文件（只查已有文件，不触发新建）
    MmapFile* f = nullptr;
    if (file_idx < ctx.sealed_files.size()) {
        f = ctx.sealed_files[file_idx].get();
    } else if (ctx.active_file &&
               ctx.sealed_files.size() == file_idx) {
        f = ctx.active_file.get();
    }
    if (!f) return false;

    TickRecord* slot = static_cast<TickRecord*>(f->at_mutable(slot_idx));
    if (!slot) return false;

    // CAS: kGap → kEmpty（临时置 kEmpty 防读端读到半写数据）
    uint8_t expected = static_cast<uint8_t>(SlotState::kGap);
    if (!slot->state.compare_exchange_strong(
            expected,
            static_cast<uint8_t>(SlotState::kEmpty),
            std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
        return false;  // 非 kGap 状态（已回补或正常写入）
    }

    // 写数据
    slot->record_type = rec.record_type;
    ::memcpy(&slot->data, &rec.data, sizeof(rec.data));

    // 最终 release store：kRecovered
    slot->state.store(static_cast<uint8_t>(SlotState::kRecovered),
                      std::memory_order_release);
    f->header()->recovered_count.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 读端接口
// ─────────────────────────────────────────────────────────────────────────────

std::vector<const MmapFile*>
MmapManager::get_tick_files(uint32_t channel) const
{
    auto it = tick_channels_.find(channel);
    if (it == tick_channels_.end()) return {};

    const ChannelCtx& ctx = it->second;
    std::vector<const MmapFile*> result;
    result.reserve(ctx.sealed_files.size() + (ctx.active_file ? 1 : 0));
    for (const auto& f : ctx.sealed_files) result.push_back(f.get());
    if (ctx.active_file) result.push_back(ctx.active_file.get());
    return result;
}

const MmapFile* MmapManager::get_snapshot_file(const char* symbol) const {
    auto it = snapshot_files_.find(symbol);
    return it == snapshot_files_.end() ? nullptr : it->second.get();
}

// ─────────────────────────────────────────────────────────────────────────────
// 统计 / 清理
// ─────────────────────────────────────────────────────────────────────────────

MmapManager::Stats MmapManager::stats() const {
    Stats s{};
    s.snapshot_drops    = snapshot_drops_.load(std::memory_order_relaxed);
    s.tick_pool_remaining = tick_pool_ ? tick_pool_->remaining() : 0;

    for (const auto& [ch, ctx] : tick_channels_) {
        s.tick_files_used += ctx.sealed_files.size() + (ctx.active_file ? 1 : 0);
        auto count_file = [&](const MmapFile* f) {
            if (!f) return;
            s.total_gap_count +=
                f->header()->gap_count.load(std::memory_order_relaxed);
            s.total_recovered +=
                f->header()->recovered_count.load(std::memory_order_relaxed);
        };
        for (const auto& f : ctx.sealed_files) count_file(f.get());
        count_file(ctx.active_file.get());
    }
    return s;
}

void MmapManager::flush_all() noexcept {
    for (auto& [sym, f] : snapshot_files_) {
        if (f) f->msync_header();
    }
    for (auto& [ch, ctx] : tick_channels_) {
        if (ctx.active_file) ctx.active_file->msync_header();
    }
}

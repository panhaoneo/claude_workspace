#pragma once
/**
 * @file mmap_manager.hpp
 * @brief 共享内存行情数据统一管理器
 *
 * MmapManager 是写端和读端的主要入口：
 *   写端：append_snapshot / append_tick / mark_gap / recover_tick
 *   读端：open_readonly 创建只读实例，get_snapshot_file / get_tick_files 访问数据
 */
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "market_records.hpp"

class MmapFile;
class TickFilePool;

// ─────────────────────────────────────────────────────────────────────────────
// StorageBackend
// ─────────────────────────────────────────────────────────────────────────────

enum class StorageBackend {
    kShmFs,   ///< /dev/shm — 全内存，延迟最低，进程退出后文件消失
    kDiskFs,  ///< 普通文件系统，持久化，支持重启恢复
};

// ─────────────────────────────────────────────────────────────────────────────
// MmapManager
// ─────────────────────────────────────────────────────────────────────────────

class MmapManager {
public:
    // ─────────────────────────────────────────────────────────────────────
    // 配置
    // ─────────────────────────────────────────────────────────────────────

    struct Config {
        StorageBackend backend           = StorageBackend::kShmFs;
        std::string    base_dir;         ///< 根目录（kShmFs 建议 "/dev/shm/market"）
        std::string    date;             ///< "YYYYMMDD"，目录隔离，不跨日

        // 快照参数
        uint64_t snapshot_capacity       = 6'000;   ///< 每个 symbol 最大条数

        // 逐笔参数
        uint64_t tick_capacity_per_file  = 1'000'000;   ///< 每文件最大条数
        uint64_t tick_total_preallocated = 500'000'000;  ///< 全量预分配条数（决定池大小）
    };

    // ─────────────────────────────────────────────────────────────────────
    // 统计
    // ─────────────────────────────────────────────────────────────────────

    struct Stats {
        uint64_t snapshot_drops;       ///< 快照写满丢弃计数
        uint64_t tick_files_used;      ///< 已分配逐笔文件数
        uint64_t tick_pool_remaining;  ///< 池中剩余文件数
        uint64_t total_gap_count;      ///< 全市场 gap slot 总数
        uint64_t total_recovered;      ///< 全市场已回补 slot 总数
    };

    // ─────────────────────────────────────────────────────────────────────
    // 构造 / 只读工厂
    // ─────────────────────────────────────────────────────────────────────

    /**
     * @brief 构造写端实例
     *
     * 会执行以下初始化：
     *   1. 创建目录树（snapshot / tick/pool / tick/{exchange}/{date}）
     *   2. TickFilePool::preallocate() — 批量预分配逐笔池文件
     *
     * @throws std::system_error  目录创建或池文件预分配失败
     */
    explicit MmapManager(const Config& cfg);

    /**
     * @brief 打开只读实例（多进程读端使用）
     *
     * 不创建池文件，不修改任何文件。返回的实例仅支持 get_* 读接口。
     *
     * @param base_dir  根目录
     * @param date      "YYYYMMDD"
     */
    static std::unique_ptr<MmapManager> open_readonly(const std::string& base_dir,
                                                       const std::string& date);

    // ─────────────────────────────────────────────────────────────────────
    // 写端接口
    // ─────────────────────────────────────────────────────────────────────

    /**
     * @brief 追加快照（单写线程）
     *
     * 若该 symbol 文件尚未创建则懒创建。
     * 文件写满后返回 -1，内部计入 snapshot_drops_。
     *
     * @param symbol    证券代码（如 "600519"）
     * @param exchange  交易所 0=SHSE 1=SZSE
     * @param rec       快照数据
     * @return slot 下标（≥0）或 -1（写满）
     */
    int64_t append_snapshot(const char*           symbol,
                            uint8_t               exchange,
                            const SnapshotRecord& rec);

    /**
     * @brief 追加逐笔（单写线程）
     *
     * seq 决定写入位置：
     *   file_idx  = (seq - 1) / tick_capacity_per_file
     *   slot_idx  = (seq - 1) % tick_capacity_per_file
     *
     * 当 slot_idx == 0 时自动从池中取文件并完成文件切换。
     *
     * @return 成功 true，池耗尽 false
     */
    bool append_tick(uint32_t            channel,
                     uint8_t             exchange,
                     uint64_t            seq,
                     const TickRecord&   rec);

    /**
     * @brief 标记 gap slot（写端检测到 seq 跳空时调用）
     *
     * 对目标 slot 执行 state.store(kGap, release)，并递增 header->gap_count。
     * 若目标文件不存在则自动创建（跨文件 gap 场景）。
     *
     * @return 成功 true，池耗尽或定位失败 false
     */
    bool mark_gap(uint32_t channel,
                  uint8_t  exchange,
                  uint64_t seq);

    /**
     * @brief 逐笔回补（回补线程调用）
     *
     * CAS(kGap → kEmpty) → 写数据 → state.store(kRecovered, release)。
     * 若 slot 不在 kGap 状态（已回补或未标 gap）则 CAS 失败，返回 false。
     *
     * @return 回补成功 true，CAS 失败或定位失败 false
     */
    bool recover_tick(uint32_t          channel,
                      uint8_t           exchange,
                      uint64_t          seq,
                      const TickRecord& rec);

    // ─────────────────────────────────────────────────────────────────────
    // 读端接口
    // ─────────────────────────────────────────────────────────────────────

    /**
     * @brief 获取某通道全部逐笔文件（sealed + active，按 file_seq 升序）
     *
     * 返回的指针在 MmapManager 生命周期内有效。
     */
    std::vector<const MmapFile*> get_tick_files(uint32_t channel) const;

    /**
     * @brief 获取某 symbol 的快照文件
     *
     * 返回 nullptr 若该 symbol 尚未写入任何数据。
     */
    const MmapFile* get_snapshot_file(const char* symbol) const;

    // ─────────────────────────────────────────────────────────────────────
    // 统计
    // ─────────────────────────────────────────────────────────────────────

    Stats stats() const;

    /// 对所有 active MmapFile 调用 msync（正常关闭时调用）
    void flush_all() noexcept;

    ~MmapManager();

    MmapManager(const MmapManager&)            = delete;
    MmapManager& operator=(const MmapManager&) = delete;

private:
    // ── 内部状态 ─────────────────────────────────────────────────────────────

    MmapManager() = default;   ///< 私有默认构造，供 open_readonly 使用

    Config cfg_;
    bool   readonly_ = false;

    std::unique_ptr<TickFilePool> tick_pool_;

    // symbol → MmapFile（快照，懒创建）
    std::unordered_map<std::string, std::unique_ptr<MmapFile>> snapshot_files_;

    // 逐笔通道上下文
    struct ChannelCtx {
        std::unique_ptr<MmapFile>              active_file;   ///< 当前写入文件
        std::vector<std::unique_ptr<MmapFile>> sealed_files;  ///< 已满文件（按 file_seq 升序）
    };
    std::unordered_map<uint32_t, ChannelCtx> tick_channels_;

    std::atomic<uint64_t> snapshot_drops_{0};

    // ── 内部方法 ─────────────────────────────────────────────────────────────

    /// 确保 channel 存在于 tick_channels_，首次调用时默认构造
    ChannelCtx& get_or_create_channel(uint32_t channel);

    /**
     * @brief 确保 seq 对应的逐笔文件存在，必要时从池中分配
     *
     * 也负责文件切换（slot_idx 回绕到 0 时密封当前文件）。
     * @return 目标 MmapFile*，池耗尽返回 nullptr
     */
    MmapFile* ensure_tick_file(ChannelCtx& ctx,
                                uint32_t    channel,
                                uint8_t     exchange,
                                uint64_t    seq);

    /**
     * @brief 从池分配一个新文件并初始化 FileHeader 的 tick 专用字段
     * @return 新 MmapFile unique_ptr，池耗尽返回 nullptr
     */
    std::unique_ptr<MmapFile> allocate_tick_file(uint32_t channel,
                                                  uint8_t  exchange,
                                                  uint32_t file_seq,
                                                  uint64_t seq_start);

    // ── 路径构建 ─────────────────────────────────────────────────────────────

    std::string snapshot_path(const char* symbol, uint8_t exchange) const;
    std::string tick_path(uint32_t channel, uint8_t exchange,
                          uint32_t file_seq) const;
    std::string pool_dir_path() const;

    static std::string exchange_str(uint8_t exchange);
};

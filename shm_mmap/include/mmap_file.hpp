#pragma once
/**
 * @file mmap_file.hpp
 * @brief 单个共享内存映射文件的 RAII 封装
 *
 * MmapFile 管理一个 mmap 文件的生命周期（open/ftruncate/mmap/munmap/close）。
 * 提供快照追加写（append_snapshot）和逐笔按 slot 写入（write_tick）两种写路径。
 * 读端使用 at() 零拷贝访问任意 slot。
 */
#include <memory>
#include <string>
#include "file_header.hpp"
#include "market_records.hpp"

class MmapFile {
public:
    // ─────────────────────────────────────────────────────────────────────────
    // 工厂方法（写端）
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief 创建快照文件（含 Index Region + Data Region）
     *
     * 文件布局：
     *   [0, kHeaderSize)                         FileHeader
     *   [kHeaderSize, kHeaderSize+cap×16)        Index Region
     *   [kHeaderSize+cap×16, ...)                Data Region（SnapshotRecord）
     *
     * @param path     文件路径（父目录须已存在）
     * @param capacity 最大 slot 数
     * @param symbol   证券代码（可为 nullptr）
     * @param exchange 交易所 0=SHSE 1=SZSE
     * @return 成功返回 unique_ptr，失败返回 nullptr
     */
    static std::unique_ptr<MmapFile> create_snapshot(
        const std::string& path,
        uint64_t           capacity,
        const char*        symbol,
        uint8_t            exchange);

    /**
     * @brief 创建逐笔文件（仅 Data Region，无 Index Region）
     *
     * 文件布局：
     *   [0, kHeaderSize)           FileHeader
     *   [kHeaderSize, ...)         Data Region（TickRecord）
     *
     * @param path      文件路径
     * @param capacity  最大 slot 数
     * @param channel   通道号（0 表示 pool 预分配阶段尚未绑定）
     * @param file_seq  同通道第几个文件（0 起）
     * @param exchange  交易所
     * @param seq_start 该文件首条 seq
     * @return 成功返回 unique_ptr，失败返回 nullptr
     */
    static std::unique_ptr<MmapFile> create_tick(
        const std::string& path,
        uint64_t           capacity,
        uint32_t           channel   = 0,
        uint16_t           file_seq  = 0,
        uint8_t            exchange  = 0,
        uint64_t           seq_start = 0);

    // ─────────────────────────────────────────────────────────────────────────
    // 工厂方法（读端 / pool acquire）
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief 以只读方式打开已有文件（读端多进程使用，MAP_SHARED|PROT_READ）
     * @return 成功返回 unique_ptr，失败返回 nullptr
     */
    static std::unique_ptr<MmapFile> open_readonly(const std::string& path);

    /**
     * @brief 以读写方式打开已有文件（pool acquire 后调用，不 truncate/不初始化头）
     * @return 成功返回 unique_ptr，失败返回 nullptr
     */
    static std::unique_ptr<MmapFile> open_readwrite(const std::string& path);

    // ─────────────────────────────────────────────────────────────────────────
    // 写端接口
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief 追加一条快照记录（单写线程调用）
     *
     * 写入顺序保证（无锁）：
     *   1. write_index.fetch_add(1, acq_rel) 声明 slot
     *   2. memcpy 到 Data Region
     *   3. IndexEntry.valid.store(1, release)  ← 读端可见性门控
     *
     * @return 成功返回 slot 下标（≥0），文件写满返回 -1
     */
    int64_t append_snapshot(const SnapshotRecord& rec);

    /**
     * @brief 按 slot_index 写入逐笔记录（seq→slot 由调用方计算）
     *
     * 写入顺序保证（无锁）：
     *   1. memcpy data 字段
     *   2. state.store(kValid, release)  ← 读端可见性门控
     *
     * @param slot_index (seq - 1) % slots_per_file
     * @param rec        TickRecord，state 字段由本函数设置，调用方无需填
     * @return 成功 true，越界 false
     */
    bool write_tick(uint64_t slot_index, const TickRecord& rec);

    // ─────────────────────────────────────────────────────────────────────────
    // 通用读端接口
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief 只读随机访问 Data Region 中的第 index 个 slot
     * @return 指向 slot 起始地址的只读指针；index 越界返回 nullptr
     */
    const void* at(uint64_t index) const noexcept;

    /**
     * @brief 可写随机访问（供 MmapManager::mark_gap / recover_tick 使用）
     * @return 指向 slot 起始地址的可写指针；只读模式或越界返回 nullptr
     */
    void* at_mutable(uint64_t index) noexcept;

    /**
     * @brief 只读访问 Index Region 第 index 个 IndexEntry（快照文件专用）
     * @return 指针，快照文件且 index 合法时有效；否则 nullptr
     */
    const IndexEntry* index_at(uint64_t index) const noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // 元信息
    // ─────────────────────────────────────────────────────────────────────────

    uint64_t size()     const noexcept;  ///< 当前 write_index（acquire load）
    uint64_t capacity() const noexcept;  ///< 文件最大 slot 数
    bool     full()     const noexcept;  ///< size() >= capacity()

    FileHeader*       header() noexcept;
    const FileHeader* header() const noexcept;

    /// 将文件头刷写到底层存储（kDiskFs 后端调用，kShmFs 下为 no-op）
    void msync_header() noexcept;

    ~MmapFile();

    // 禁止拷贝
    MmapFile(const MmapFile&)            = delete;
    MmapFile& operator=(const MmapFile&) = delete;

private:
    MmapFile(int fd, void* base, size_t mapped_size,
             size_t slot_size, bool readonly) noexcept;

    int    fd_;
    void*  base_;
    size_t mapped_size_;
    size_t slot_size_;
    bool   readonly_;
};

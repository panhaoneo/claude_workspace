#pragma once
/**
 * @file file_header.hpp
 * @brief 共享内存文件头结构定义
 *
 * FileHeader 占 kHeaderSize(4096) 字节，位于每个 .mmap 文件起始处。
 * 快照文件和逐笔文件共用同一 FileHeader 布局，通过 data_type 字段区分。
 */
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

// ─────────────────────────────────────────────────────────────────────────────
// 常量
// ─────────────────────────────────────────────────────────────────────────────

/// 文件头大小，固定 4096 字节（一个内存页）
static constexpr size_t kHeaderSize = 4096;

/// Magic 标识：快照文件（'QDSH' per design）
static constexpr uint64_t kMagicSnapshot = 0x51445348ULL;

/// Magic 标识：逐笔文件（'QDTT' per design）
static constexpr uint64_t kMagicTick = 0x51445454ULL;

/// 布局版本号，当前 v1
static constexpr uint32_t kVersion = 1;

// ─────────────────────────────────────────────────────────────────────────────
// 工具函数
// ─────────────────────────────────────────────────────────────────────────────

/// 返回当前时间（epoch nanoseconds）
inline uint64_t now_ns() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
         + static_cast<uint64_t>(ts.tv_nsec);
}

// ─────────────────────────────────────────────────────────────────────────────
// FileHeader
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 共享内存文件头（固定 4096 字节）
 *
 * 内存布局（无隐式填充，各字段手动对齐）：
 *   [  0] magic(8)           快照=kMagicSnapshot 逐笔=kMagicTick
 *   [  8] version(4)         布局版本，当前 1
 *   [ 12] slot_size(4)       sizeof(Record)，编译期固定
 *   [ 16] capacity(8)        文件最大 slot 数
 *   [ 24] write_index(8)     已写/已声明 slot 数，单调递增（atomic）
 *   [ 32] exchange(1)        0=SHSE 1=SZSE
 *   [ 33] data_type(1)       0=Snapshot 1=Tick
 *   [ 34] _pad0[6]
 *   [ 40] created_at(8)      epoch ns
 *   [ 48] last_written_at(8) epoch ns，周期更新供监控
 *   ── 快照专用 ──────────────────────────────────────────────
 *   [ 56] symbol[32]         股票代码
 *   [ 88] index_offset(8)    Index Region 起始偏移（= kHeaderSize）
 *   [ 96] data_offset(8)     Data Region 起始偏移
 *   ── 逐笔专用 ──────────────────────────────────────────────
 *   [104] channel(4)         通道号
 *   [108] file_seq(2)        同通道第几个文件（0 起）
 *   [110] _pad1[2]
 *   [112] seq_start(8)       该文件首条 seq
 *   [120] seq_end(8)         该文件末条 seq（写满后设置，0 表示未满）
 *   [128] gap_count(4)       gap slot 数量（atomic）
 *   [132] recovered_count(4) 已回补 slot 数量（atomic）
 *   ── 保留 ───────────────────────────────────────────────────
 *   [136] _reserved[3960]
 *   [4096] end
 */
struct FileHeader {
    // ── 通用字段 ─────────────────────────────────────────────────────────────
    uint64_t             magic;            ///< kMagicSnapshot 或 kMagicTick
    uint32_t             version;          ///< 布局版本
    uint32_t             slot_size;        ///< sizeof(Record)
    uint64_t             capacity;         ///< 文件最大 slot 数
    std::atomic<uint64_t> write_index;     ///< 已声明/已写 slot 数（acq_rel）

    uint8_t              exchange;         ///< 0=SHSE 1=SZSE
    uint8_t              data_type;        ///< 0=Snapshot 1=Tick
    uint8_t              _pad0[6];

    uint64_t             created_at;       ///< epoch ns
    uint64_t             last_written_at;  ///< epoch ns

    // ── 快照专用字段 ─────────────────────────────────────────────────────────
    char                 symbol[32];       ///< 证券代码
    uint64_t             index_offset;     ///< Index Region 起始偏移（= kHeaderSize）
    uint64_t             data_offset;      ///< Data Region 起始偏移

    // ── 逐笔专用字段 ─────────────────────────────────────────────────────────
    uint32_t             channel;          ///< 通道号
    uint16_t             file_seq;         ///< 同通道第几个文件（0 起）
    uint8_t              _pad1[2];
    uint64_t             seq_start;        ///< 该文件首条 seq（逐笔）
    uint64_t             seq_end;          ///< 该文件末条 seq，0=未写满（逐笔）
    std::atomic<uint32_t> gap_count;       ///< gap slot 数量
    std::atomic<uint32_t> recovered_count; ///< 已回补 slot 数量

    // ── 保留填充，补齐到 4096 字节 ───────────────────────────────────────────
    uint8_t              _reserved[3960];
};
static_assert(sizeof(FileHeader) == kHeaderSize,
              "FileHeader must be exactly 4096 bytes");

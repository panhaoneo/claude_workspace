#pragma once
/**
 * @file market_records.hpp
 * @brief 共享内存 slot 数据结构定义
 *
 * 包含：
 *   - SlotState   逐笔 slot 就绪状态枚举
 *   - IndexEntry  快照时间索引条目（16 字节）
 *   - SnapshotRecord  快照数据 slot（256 字节，alignas(64)）
 *   - TickRecord      逐笔数据 slot（128 字节，alignas(64)）
 */
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "xtp_types.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// SlotState
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 逐笔文件 per-slot 就绪状态
 *
 * ftruncate 后内存全零，kEmpty 默认值为 0，读端可直接检查。
 *
 * 状态转换：
 *   kEmpty     → kValid      （正常写入）
 *   kEmpty     → kGap        （确认丢包，skip 等回补）
 *   kGap       → kEmpty      （回补线程 CAS，中间态）
 *   kEmpty     → kRecovered  （回补线程完成写入）
 */
enum class SlotState : uint8_t {
    kEmpty     = 0,   ///< 未写入（ftruncate 全零默认值）
    kValid     = 1,   ///< 正常写入，数据就绪
    kGap       = 2,   ///< 确认丢包，等待回补
    kRecovered = 3,   ///< 已回补，数据就绪
};

// ─────────────────────────────────────────────────────────────────────────────
// IndexEntry  — 快照 Index Region 条目
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 快照文件 Index Region 单条目（16 字节）
 *
 * 并发写读协议：
 *   写端：memcpy(data_region) → valid.store(1, release)
 *   读端：valid.load(acquire) → 访问 data_region
 *
 * valid 使用 std::atomic<uint8_t> 保证跨进程 release/acquire 语义。
 */
struct IndexEntry {
    int64_t              data_time;   ///< 行情时间 YYYYMMDDHHMMSSmmm
    uint32_t             slot_index;  ///< 对应 Data Region 的 slot 下标（冗余，方便读端）
    std::atomic<uint8_t> valid;       ///< 0=未写 1=有效，release store 写，acquire load 读
    uint8_t              _pad[3];
};
static_assert(sizeof(IndexEntry) == 16, "IndexEntry must be exactly 16 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// SnapshotRecord  — 快照数据 slot
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 快照数据 slot（alignas(64)，cache line 对齐）
 *
 * sizeof = 256 字节（4 个 64-byte cache line）。
 * XQUOTE_MARKET_DATA 本身即 256 字节，无需额外填充。
 */
struct alignas(64) SnapshotRecord {
    XQUOTE_MARKET_DATA data;
    // sizeof(XQUOTE_MARKET_DATA) == 256 == 4×64，无需 _pad
};
static_assert(sizeof(SnapshotRecord) == 256,
              "SnapshotRecord must be exactly 256 bytes");
static_assert(alignof(SnapshotRecord) == 64,
              "SnapshotRecord must be 64-byte aligned");

// ─────────────────────────────────────────────────────────────────────────────
// TickRecord  — 逐笔数据 slot
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 逐笔数据 slot（alignas(64)，2 个 cache line）
 *
 * sizeof = 128 字节
 * 内存布局：
 *   [  0] state(1)       ← SlotState，写端最后写，读端最先读
 *   [  1] record_type(1) ← 0=Transaction 1=Order
 *   [  2] _pad0[6]
 *   [  8] data union     ← XQUOTE_TRANSACTION(88B) 或 XQUOTE_ORDER(64B)
 *   [ 96] _pad1[32]      ← 补齐到 128 字节（2×64）
 *   [128] end
 *
 * 并发写读协议：
 *   写端（正常）：写 data 字段 → state.store(kValid, release)
 *   写端（gap）  ：state.store(kGap, release)（data 字段不写）
 *   回补线程    ：state.CAS(kGap→kEmpty) → 写 data → state.store(kRecovered, release)
 *   读端        ：state.load(acquire) → 检查 kValid/kRecovered → 读 data
 *
 * state 位于 offset 0，读端只需读首字节即可判断就绪状态，对 CPU prefetch 友好。
 */
struct alignas(64) TickRecord {
    std::atomic<uint8_t> state;        ///< SlotState，**必须最先定义**（offset 0）
    uint8_t              record_type;  ///< 0=Transaction 1=Order
    uint8_t              _pad0[6];
    union {
        XQUOTE_TRANSACTION transaction; ///< sizeof = 88
        XQUOTE_ORDER       order;       ///< sizeof = 64
    } data;                            ///< offset 8，size 88（取 union 最大值）
    uint8_t              _pad1[32];    ///< 补齐：8 + 88 + 32 = 128 字节

    // std::atomic 删除了默认拷贝/移动构造，显式实现以支持按值传递（relaxed 语义）
    TickRecord() noexcept { state.store(0, std::memory_order_relaxed); }

    TickRecord(const TickRecord& o) noexcept {
        state.store(o.state.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
        record_type = o.record_type;
        ::memcpy(&data,  &o.data,  sizeof(data));
        ::memcpy(_pad1, o._pad1, sizeof(_pad1));
    }
    TickRecord& operator=(const TickRecord& o) noexcept {
        if (this != &o) {
            state.store(o.state.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
            record_type = o.record_type;
            ::memcpy(&data,  &o.data,  sizeof(data));
            ::memcpy(_pad1, o._pad1, sizeof(_pad1));
        }
        return *this;
    }
    TickRecord(TickRecord&& o) noexcept : TickRecord(o) {}
    TickRecord& operator=(TickRecord&& o) noexcept { return *this = o; }
};
static_assert(sizeof(TickRecord) == 128,
              "TickRecord must be exactly 128 bytes");
static_assert(alignof(TickRecord) == 64,
              "TickRecord must be 64-byte aligned");

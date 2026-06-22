/**
 * @file test_mmap_file.cpp
 * @brief MmapFile 单元测试：预分配、追加、at()、写满边界、只读模式
 */
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <sys/stat.h>

#include "mmap_file.hpp"
#include "file_header.hpp"
#include "market_records.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// 测试夹具
// ─────────────────────────────────────────────────────────────────────────────

class MmapFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        char tmpl[] = "/tmp/mmap_file_test_XXXXXX";
        const char* d = ::mkdtemp(tmpl);
        ASSERT_NE(d, nullptr) << "mkdtemp failed";
        test_dir_ = d;
    }

    void TearDown() override {
        if (!test_dir_.empty()) {
            std::string cmd = "rm -rf " + test_dir_;
            ::system(cmd.c_str());
        }
    }

    std::string path(const std::string& name) {
        return test_dir_ + "/" + name;
    }

    /// 构造一个填充了有效数据的 SnapshotRecord
    static SnapshotRecord make_snapshot(int64_t data_time, double price) {
        SnapshotRecord rec{};
        rec.data.data_time  = data_time;
        rec.data.last_price = price;
        ::strncpy(rec.data.ticker, "600519", sizeof(rec.data.ticker));
        return rec;
    }

    /// 构造一个填充了有效数据的 TickRecord（Transaction）
    static TickRecord make_tick(int64_t seq, double price) {
        TickRecord rec{};
        rec.record_type = 0;  // Transaction
        rec.data.transaction.seq   = seq;
        rec.data.transaction.price = price;
        ::strncpy(rec.data.transaction.ticker, "000001",
                  sizeof(rec.data.transaction.ticker));
        return rec;
    }

    std::string test_dir_;
};

// ─────────────────────────────────────────────────────────────────────────────
// 快照文件测试
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(MmapFileTest, CreateSnapshot_FileExists) {
    auto f = MmapFile::create_snapshot(path("snap.mmap"), 100, "600519", 0);
    ASSERT_NE(f, nullptr);

    struct stat st;
    ASSERT_EQ(::stat(path("snap.mmap").c_str(), &st), 0);

    // 文件大小 = kHeaderSize + 100×sizeof(IndexEntry) + 100×sizeof(SnapshotRecord)
    size_t expected = kHeaderSize
                    + 100 * sizeof(IndexEntry)
                    + 100 * sizeof(SnapshotRecord);
    EXPECT_EQ(static_cast<size_t>(st.st_size), expected);
}

TEST_F(MmapFileTest, CreateSnapshot_HeaderFields) {
    auto f = MmapFile::create_snapshot(path("snap.mmap"), 200, "600519", 0);
    ASSERT_NE(f, nullptr);

    const FileHeader* hdr = f->header();
    EXPECT_EQ(hdr->magic,       kMagicSnapshot);
    EXPECT_EQ(hdr->version,     kVersion);
    EXPECT_EQ(hdr->slot_size,   static_cast<uint32_t>(sizeof(SnapshotRecord)));
    EXPECT_EQ(hdr->capacity,    200u);
    EXPECT_EQ(hdr->data_type,   0u);  // Snapshot
    EXPECT_EQ(hdr->exchange,    0u);  // SHSE
    EXPECT_STREQ(hdr->symbol,   "600519");
    EXPECT_EQ(hdr->index_offset, kHeaderSize);
    EXPECT_EQ(hdr->data_offset,  kHeaderSize + 200 * sizeof(IndexEntry));
    EXPECT_EQ(f->size(),         0u);
    EXPECT_EQ(f->capacity(),     200u);
    EXPECT_FALSE(f->full());
}

TEST_F(MmapFileTest, AppendSnapshot_SizeIncrements) {
    auto f = MmapFile::create_snapshot(path("snap.mmap"), 50, "600519", 0);
    ASSERT_NE(f, nullptr);

    EXPECT_EQ(f->size(), 0u);

    auto rec = make_snapshot(20250408093000001LL, 1800.0);
    int64_t idx = f->append_snapshot(rec);
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(f->size(), 1u);

    idx = f->append_snapshot(rec);
    EXPECT_EQ(idx, 1);
    EXPECT_EQ(f->size(), 2u);
}

TEST_F(MmapFileTest, AppendSnapshot_DataReadback) {
    auto f = MmapFile::create_snapshot(path("snap.mmap"), 50, "600519", 0);
    ASSERT_NE(f, nullptr);

    auto rec = make_snapshot(20250408093000001LL, 1866.50);
    f->append_snapshot(rec);

    // 验证 IndexEntry
    const IndexEntry* ie = f->index_at(0);
    ASSERT_NE(ie, nullptr);
    EXPECT_EQ(ie->data_time, 20250408093000001LL);
    EXPECT_EQ(ie->slot_index, 0u);
    EXPECT_EQ(ie->valid.load(std::memory_order_acquire), 1u);

    // 验证数据 slot
    const SnapshotRecord* sr =
        static_cast<const SnapshotRecord*>(f->at(0));
    ASSERT_NE(sr, nullptr);
    EXPECT_DOUBLE_EQ(sr->data.last_price, 1866.50);
    EXPECT_STREQ(sr->data.ticker, "600519");
}

TEST_F(MmapFileTest, AppendSnapshot_FullReturnsNegOne) {
    const uint64_t cap = 10;
    auto f = MmapFile::create_snapshot(path("snap.mmap"), cap, "600519", 0);
    ASSERT_NE(f, nullptr);

    auto rec = make_snapshot(1LL, 100.0);
    for (uint64_t i = 0; i < cap; ++i) {
        EXPECT_GE(f->append_snapshot(rec), 0);
    }
    EXPECT_TRUE(f->full());

    // 写满后继续追加应返回 -1
    EXPECT_EQ(f->append_snapshot(rec), -1);
    EXPECT_EQ(f->append_snapshot(rec), -1);
}

TEST_F(MmapFileTest, At_OutOfBoundsReturnsNullptr) {
    auto f = MmapFile::create_snapshot(path("snap.mmap"), 10, "600519", 0);
    ASSERT_NE(f, nullptr);

    EXPECT_NE(f->at(9), nullptr);
    EXPECT_EQ(f->at(10), nullptr);
    EXPECT_EQ(f->at(999), nullptr);
}

TEST_F(MmapFileTest, OpenReadonly_VerifyData) {
    {
        auto f = MmapFile::create_snapshot(path("snap.mmap"), 20, "600519", 0);
        ASSERT_NE(f, nullptr);
        auto rec = make_snapshot(20250408100000001LL, 2100.0);
        f->append_snapshot(rec);
    }  // 关闭写端

    // 以只读方式重新打开
    auto f = MmapFile::open_readonly(path("snap.mmap"));
    ASSERT_NE(f, nullptr);

    EXPECT_EQ(f->capacity(), 20u);
    EXPECT_EQ(f->size(), 1u);

    const SnapshotRecord* sr =
        static_cast<const SnapshotRecord*>(f->at(0));
    ASSERT_NE(sr, nullptr);
    EXPECT_DOUBLE_EQ(sr->data.last_price, 2100.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 逐笔文件测试
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(MmapFileTest, CreateTick_FileExists) {
    auto f = MmapFile::create_tick(path("tick.mmap"), 1000, 1, 0, 0, 1);
    ASSERT_NE(f, nullptr);

    struct stat st;
    ASSERT_EQ(::stat(path("tick.mmap").c_str(), &st), 0);

    size_t expected = kHeaderSize + 1000 * sizeof(TickRecord);
    EXPECT_EQ(static_cast<size_t>(st.st_size), expected);
}

TEST_F(MmapFileTest, CreateTick_HeaderFields) {
    auto f = MmapFile::create_tick(path("tick.mmap"), 500, 3, 2, 1, 500001);
    ASSERT_NE(f, nullptr);

    const FileHeader* hdr = f->header();
    EXPECT_EQ(hdr->magic,       kMagicTick);
    EXPECT_EQ(hdr->slot_size,   static_cast<uint32_t>(sizeof(TickRecord)));
    EXPECT_EQ(hdr->capacity,    500u);
    EXPECT_EQ(hdr->data_type,   1u);    // Tick
    EXPECT_EQ(hdr->channel,     3u);
    EXPECT_EQ(hdr->file_seq,    2u);
    EXPECT_EQ(hdr->exchange,    1u);    // SZSE
    EXPECT_EQ(hdr->seq_start,   500001u);
    EXPECT_EQ(hdr->seq_end,     0u);    // 未写满
    EXPECT_EQ(hdr->data_offset, kHeaderSize);
}

TEST_F(MmapFileTest, WriteTick_DataReadback) {
    auto f = MmapFile::create_tick(path("tick.mmap"), 100, 1, 0, 0, 1);
    ASSERT_NE(f, nullptr);

    auto rec = make_tick(42, 15.88);
    EXPECT_TRUE(f->write_tick(0, rec));

    const TickRecord* slot =
        static_cast<const TickRecord*>(f->at(0));
    ASSERT_NE(slot, nullptr);

    // 验证 state
    EXPECT_EQ(slot->state.load(std::memory_order_acquire),
              static_cast<uint8_t>(SlotState::kValid));
    EXPECT_EQ(slot->data.transaction.seq,   42LL);
    EXPECT_DOUBLE_EQ(slot->data.transaction.price, 15.88);
}

TEST_F(MmapFileTest, WriteTick_MultipleSlots) {
    auto f = MmapFile::create_tick(path("tick.mmap"), 100, 1, 0, 0, 1);
    ASSERT_NE(f, nullptr);

    for (uint64_t i = 0; i < 50; ++i) {
        auto rec = make_tick(static_cast<int64_t>(i + 1),
                             static_cast<double>(i) * 0.01 + 10.0);
        EXPECT_TRUE(f->write_tick(i, rec));
    }

    for (uint64_t i = 0; i < 50; ++i) {
        const TickRecord* slot =
            static_cast<const TickRecord*>(f->at(i));
        ASSERT_NE(slot, nullptr);
        EXPECT_EQ(slot->state.load(std::memory_order_acquire),
                  static_cast<uint8_t>(SlotState::kValid));
        EXPECT_EQ(slot->data.transaction.seq, static_cast<int64_t>(i + 1));
    }
}

TEST_F(MmapFileTest, WriteTick_OutOfBoundsReturnsFalse) {
    auto f = MmapFile::create_tick(path("tick.mmap"), 10, 1, 0, 0, 1);
    ASSERT_NE(f, nullptr);

    auto rec = make_tick(1, 1.0);
    EXPECT_TRUE(f->write_tick(9, rec));
    EXPECT_FALSE(f->write_tick(10, rec));
    EXPECT_FALSE(f->write_tick(999, rec));
}

TEST_F(MmapFileTest, SlotState_InitiallyEmpty) {
    auto f = MmapFile::create_tick(path("tick.mmap"), 10, 1, 0, 0, 1);
    ASSERT_NE(f, nullptr);

    // ftruncate 后全零，state 默认 kEmpty
    const TickRecord* slot =
        static_cast<const TickRecord*>(f->at(5));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->state.load(std::memory_order_acquire),
              static_cast<uint8_t>(SlotState::kEmpty));
}

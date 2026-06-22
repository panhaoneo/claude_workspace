/**
 * @file test_snapshot.cpp
 * @brief 快照模块测试：时间索引、写满 drop 计数、多 symbol 隔离
 */
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

#include "mmap_manager.hpp"
#include "mmap_file.hpp"
#include "file_header.hpp"
#include "market_records.hpp"

class SnapshotTest : public ::testing::Test {
protected:
    void SetUp() override {
        char tmpl[] = "/tmp/snapshot_test_XXXXXX";
        const char* d = ::mkdtemp(tmpl);
        ASSERT_NE(d, nullptr);
        test_dir_ = d;

        MmapManager::Config cfg;
        cfg.base_dir               = test_dir_;
        cfg.date                   = "20250408";
        cfg.snapshot_capacity      = 50;
        cfg.tick_capacity_per_file = 1000;
        cfg.tick_total_preallocated = 3000;  // 3 个 pool 文件

        mgr_ = std::make_unique<MmapManager>(cfg);
    }

    void TearDown() override {
        mgr_.reset();
        if (!test_dir_.empty()) {
            std::string cmd = "rm -rf " + test_dir_;
            ::system(cmd.c_str());
        }
    }

    static SnapshotRecord make_rec(const char* ticker, int64_t ts,
                                   double last_price) {
        SnapshotRecord rec{};
        ::strncpy(rec.data.ticker, ticker, sizeof(rec.data.ticker) - 1);
        rec.data.data_time  = ts;
        rec.data.last_price = last_price;
        rec.data.exchange   = 1;  // SHSE
        return rec;
    }

    std::string test_dir_;
    std::unique_ptr<MmapManager> mgr_;
};

// ─────────────────────────────────────────────────────────────────────────────
// 基本写入 / 读取
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SnapshotTest, AppendAndRead_SingleRecord) {
    auto rec = make_rec("600519", 20250408093000001LL, 1800.0);
    int64_t idx = mgr_->append_snapshot("600519", 0, rec);
    EXPECT_EQ(idx, 0);

    const MmapFile* f = mgr_->get_snapshot_file("600519");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->size(), 1u);

    const SnapshotRecord* sr =
        static_cast<const SnapshotRecord*>(f->at(0));
    ASSERT_NE(sr, nullptr);
    EXPECT_DOUBLE_EQ(sr->data.last_price, 1800.0);
    EXPECT_EQ(sr->data.data_time, 20250408093000001LL);
}

TEST_F(SnapshotTest, AppendAndRead_MultipleRecords) {
    const int N = 30;
    for (int i = 0; i < N; ++i) {
        auto rec = make_rec("000001",
                            20250408090000000LL + i * 3000,
                            10.0 + i * 0.01);
        int64_t idx = mgr_->append_snapshot("000001", 0, rec);
        EXPECT_EQ(idx, i);
    }

    const MmapFile* f = mgr_->get_snapshot_file("000001");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->size(), static_cast<uint64_t>(N));

    for (int i = 0; i < N; ++i) {
        const IndexEntry* ie = f->index_at(static_cast<uint64_t>(i));
        ASSERT_NE(ie, nullptr);
        EXPECT_EQ(ie->valid.load(std::memory_order_acquire), 1u);
        EXPECT_EQ(ie->data_time, 20250408090000000LL + i * 3000);

        const SnapshotRecord* sr =
            static_cast<const SnapshotRecord*>(f->at(static_cast<uint64_t>(i)));
        ASSERT_NE(sr, nullptr);
        EXPECT_NEAR(sr->data.last_price, 10.0 + i * 0.01, 1e-9);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 写满 / drop 计数
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SnapshotTest, DropCount_WhenFull) {
    const int CAP = 50;  // snapshot_capacity

    auto rec = make_rec("600519", 1LL, 100.0);

    // 写满（50 条）
    for (int i = 0; i < CAP; ++i) {
        EXPECT_GE(mgr_->append_snapshot("600519", 0, rec), 0);
    }

    EXPECT_EQ(mgr_->stats().snapshot_drops, 0u);

    // 继续写入：应被丢弃并计数
    EXPECT_EQ(mgr_->append_snapshot("600519", 0, rec), -1);
    EXPECT_EQ(mgr_->append_snapshot("600519", 0, rec), -1);
    EXPECT_EQ(mgr_->stats().snapshot_drops, 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 多 symbol 隔离
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SnapshotTest, MultiSymbol_Independent) {
    auto rec_a = make_rec("600519", 100LL, 1800.0);
    auto rec_b = make_rec("000001", 200LL, 15.0);

    mgr_->append_snapshot("600519", 0, rec_a);
    mgr_->append_snapshot("600519", 0, rec_a);
    mgr_->append_snapshot("000001", 1, rec_b);

    const MmapFile* fa = mgr_->get_snapshot_file("600519");
    const MmapFile* fb = mgr_->get_snapshot_file("000001");

    ASSERT_NE(fa, nullptr);
    ASSERT_NE(fb, nullptr);
    EXPECT_NE(fa, fb);  // 不同文件对象

    EXPECT_EQ(fa->size(), 2u);
    EXPECT_EQ(fb->size(), 1u);

    const SnapshotRecord* sb = static_cast<const SnapshotRecord*>(fb->at(0));
    ASSERT_NE(sb, nullptr);
    EXPECT_DOUBLE_EQ(sb->data.last_price, 15.0);
}

TEST_F(SnapshotTest, GetSnapshotFile_NullForUnknownSymbol) {
    EXPECT_EQ(mgr_->get_snapshot_file("999999"), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Index Region：时间序扫描
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SnapshotTest, IndexRegion_TimeRangeScan) {
    // 写入 10 条，时间递增
    for (int i = 0; i < 10; ++i) {
        auto rec = make_rec("600519",
                            20250408093000000LL + i * 1000,
                            1800.0 + i);
        mgr_->append_snapshot("600519", 0, rec);
    }

    const MmapFile* f = mgr_->get_snapshot_file("600519");
    ASSERT_NE(f, nullptr);

    // 扫描 [ts_start, ts_end] 范围内的条目
    int64_t ts_start = 20250408093003000LL;
    int64_t ts_end   = 20250408093007000LL;

    std::vector<const SnapshotRecord*> found;
    uint64_t sz = f->size();
    for (uint64_t i = 0; i < sz; ++i) {
        const IndexEntry* ie = f->index_at(i);
        if (!ie || ie->valid.load(std::memory_order_acquire) == 0) continue;
        if (ie->data_time >= ts_start && ie->data_time <= ts_end) {
            const SnapshotRecord* sr =
                static_cast<const SnapshotRecord*>(f->at(ie->slot_index));
            if (sr) found.push_back(sr);
        }
    }

    // 期望找到 ts=...003000 到 ...007000，即 5 条
    EXPECT_EQ(found.size(), 5u);
    for (const auto* sr : found) {
        EXPECT_GE(sr->data.data_time, ts_start);
        EXPECT_LE(sr->data.data_time, ts_end);
    }
}

TEST_F(SnapshotTest, IndexRegion_LatestSnapshot) {
    for (int i = 0; i < 5; ++i) {
        auto rec = make_rec("600519", 1000LL + i, 100.0 + i);
        mgr_->append_snapshot("600519", 0, rec);
    }

    const MmapFile* f = mgr_->get_snapshot_file("600519");
    ASSERT_NE(f, nullptr);

    // 最新快照：write_index - 1
    uint64_t last = f->size() - 1;
    const SnapshotRecord* sr =
        static_cast<const SnapshotRecord*>(f->at(last));
    ASSERT_NE(sr, nullptr);
    EXPECT_DOUBLE_EQ(sr->data.last_price, 104.0);  // 100.0 + 4
}

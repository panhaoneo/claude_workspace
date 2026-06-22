/**
 * @file test_mmap_manager.cpp
 * @brief MmapManager 集成测试：快照 + 逐笔混合写入、统计、get_* 接口
 */
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

#include "mmap_manager.hpp"
#include "mmap_file.hpp"
#include "file_header.hpp"
#include "market_records.hpp"

class MmapManagerTest : public ::testing::Test {
protected:
    static constexpr uint64_t kSlotsPerFile = 50;

    void SetUp() override {
        char tmpl[] = "/tmp/mgr_test_XXXXXX";
        const char* d = ::mkdtemp(tmpl);
        ASSERT_NE(d, nullptr);
        test_dir_ = d;

        MmapManager::Config cfg;
        cfg.base_dir                = test_dir_;
        cfg.date                    = "20250408";
        cfg.snapshot_capacity       = 20;
        cfg.tick_capacity_per_file  = kSlotsPerFile;
        cfg.tick_total_preallocated = kSlotsPerFile * 4;  // 4 pool 文件

        mgr_ = std::make_unique<MmapManager>(cfg);
    }

    void TearDown() override {
        mgr_.reset();
        if (!test_dir_.empty()) {
            std::string cmd = "rm -rf " + test_dir_;
            ::system(cmd.c_str());
        }
    }

    static SnapshotRecord make_snap(const char* sym, int64_t ts, double price) {
        SnapshotRecord r{};
        ::strncpy(r.data.ticker, sym, sizeof(r.data.ticker) - 1);
        r.data.data_time  = ts;
        r.data.last_price = price;
        return r;
    }

    static TickRecord make_tick(int64_t seq, double price) {
        TickRecord r{};
        r.record_type = 0;
        r.data.transaction.seq   = seq;
        r.data.transaction.price = price;
        return r;
    }

    std::string test_dir_;
    std::unique_ptr<MmapManager> mgr_;
};

// ─────────────────────────────────────────────────────────────────────────────
// 快照 + 逐笔混合
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(MmapManagerTest, Mixed_SnapshotAndTick) {
    // 写 5 条快照
    for (int i = 0; i < 5; ++i) {
        auto r = make_snap("600519", 1000LL + i, 1800.0 + i);
        EXPECT_GE(mgr_->append_snapshot("600519", 0, r), 0);
    }
    // 写 10 条逐笔
    for (uint64_t seq = 1; seq <= 10; ++seq) {
        EXPECT_TRUE(mgr_->append_tick(1, 0, seq, make_tick(seq, 10.0)));
    }

    // 快照验证
    const MmapFile* sf = mgr_->get_snapshot_file("600519");
    ASSERT_NE(sf, nullptr);
    EXPECT_EQ(sf->size(), 5u);

    // 逐笔验证
    auto tfiles = mgr_->get_tick_files(1);
    ASSERT_EQ(tfiles.size(), 1u);
    EXPECT_EQ(tfiles[0]->size(), 10u);
}

// ─────────────────────────────────────────────────────────────────────────────
// stats()
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(MmapManagerTest, Stats_InitialState) {
    auto s = mgr_->stats();
    EXPECT_EQ(s.snapshot_drops,     0u);
    EXPECT_EQ(s.tick_files_used,    0u);
    EXPECT_EQ(s.total_gap_count,    0u);
    EXPECT_EQ(s.total_recovered,    0u);
    EXPECT_EQ(s.tick_pool_remaining, 4u);
}

TEST_F(MmapManagerTest, Stats_AfterWrites) {
    // 写 1 条快照
    mgr_->append_snapshot("000001", 1, make_snap("000001", 1, 10.0));

    // 写 1 条逐笔（分配第一个池文件）
    mgr_->append_tick(1, 0, 1, make_tick(1, 1.0));

    // 标记 2 个 gap
    mgr_->mark_gap(1, 0, 5);
    mgr_->mark_gap(1, 0, 6);

    // 回补 1 个
    mgr_->recover_tick(1, 0, 5, make_tick(5, 5.0));

    auto s = mgr_->stats();
    EXPECT_EQ(s.tick_files_used,    1u);
    EXPECT_EQ(s.tick_pool_remaining, 3u);
    EXPECT_EQ(s.total_gap_count,    2u);
    EXPECT_EQ(s.total_recovered,    1u);
}

TEST_F(MmapManagerTest, Stats_SnapshotDrops) {
    // 写满 20 条（capacity=20）
    for (int i = 0; i < 20; ++i)
        mgr_->append_snapshot("600519", 0, make_snap("600519", i, 100.0));

    EXPECT_EQ(mgr_->stats().snapshot_drops, 0u);

    // 超出 capacity
    mgr_->append_snapshot("600519", 0, make_snap("600519", 99, 100.0));
    mgr_->append_snapshot("600519", 0, make_snap("600519", 100, 100.0));
    EXPECT_EQ(mgr_->stats().snapshot_drops, 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_tick_files 排序
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(MmapManagerTest, GetTickFiles_OrderedByFileSeq) {
    // 写入 2 个完整文件 + 1 条（总共 2 个切换，3 个文件）
    for (uint64_t seq = 1; seq <= kSlotsPerFile * 2 + 1; ++seq) {
        ASSERT_TRUE(mgr_->append_tick(1, 0, seq, make_tick(seq, 1.0)));
    }

    auto files = mgr_->get_tick_files(1);
    ASSERT_EQ(files.size(), 3u);

    for (size_t i = 0; i < files.size(); ++i) {
        EXPECT_EQ(files[i]->header()->file_seq, static_cast<uint16_t>(i))
            << "file_seq mismatch at index " << i;
    }

    EXPECT_EQ(files[0]->header()->seq_start, 1u);
    EXPECT_EQ(files[1]->header()->seq_start, kSlotsPerFile + 1);
    EXPECT_EQ(files[2]->header()->seq_start, kSlotsPerFile * 2 + 1);
}

TEST_F(MmapManagerTest, GetTickFiles_EmptyForUnknownChannel) {
    auto files = mgr_->get_tick_files(999);
    EXPECT_TRUE(files.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// flush_all 不崩溃
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(MmapManagerTest, FlushAll_NoCrash) {
    mgr_->append_snapshot("600519", 0, make_snap("600519", 1, 100.0));
    mgr_->append_tick(1, 0, 1, make_tick(1, 1.0));
    EXPECT_NO_THROW(mgr_->flush_all());
}

// ─────────────────────────────────────────────────────────────────────────────
// 多 symbol 多 channel
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(MmapManagerTest, MultiSymbolMultiChannel) {
    const char* symbols[] = {"600519", "000001", "000002"};
    for (const char* sym : symbols) {
        for (int i = 0; i < 5; ++i) {
            mgr_->append_snapshot(sym, 0,
                                  make_snap(sym, 1000LL + i, 100.0 + i));
        }
    }

    for (uint32_t ch = 1; ch <= 3; ++ch) {
        for (uint64_t seq = 1; seq <= 10; ++seq) {
            mgr_->append_tick(ch, 0, seq, make_tick(seq, static_cast<double>(ch)));
        }
    }

    for (const char* sym : symbols) {
        const MmapFile* f = mgr_->get_snapshot_file(sym);
        ASSERT_NE(f, nullptr) << sym;
        EXPECT_EQ(f->size(), 5u) << sym;
    }

    for (uint32_t ch = 1; ch <= 3; ++ch) {
        auto files = mgr_->get_tick_files(ch);
        ASSERT_FALSE(files.empty()) << "channel=" << ch;
        EXPECT_EQ(files[0]->header()->channel, ch);
    }

    // 所有快照文件彼此不同
    const MmapFile* f0 = mgr_->get_snapshot_file("600519");
    const MmapFile* f1 = mgr_->get_snapshot_file("000001");
    const MmapFile* f2 = mgr_->get_snapshot_file("000002");
    EXPECT_NE(f0, f1);
    EXPECT_NE(f1, f2);
}

// ─────────────────────────────────────────────────────────────────────────────
// pool 耗尽保护
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(MmapManagerTest, PoolExhaustion_ReturnsFalse) {
    // pool 有 4 个文件，写满 4 个文件后再触发切换应失败
    uint64_t total = kSlotsPerFile * 4 + 1;
    bool last_ok = true;
    for (uint64_t seq = 1; seq <= total; ++seq) {
        bool ok = mgr_->append_tick(1, 0, seq, make_tick(seq, 1.0));
        if (seq == total) last_ok = ok;
    }
    EXPECT_FALSE(last_ok);  // 池耗尽
}

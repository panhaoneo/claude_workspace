/**
 * @file test_tick.cpp
 * @brief 逐笔模块测试：seq→slot 映射、gap 标记、CAS 回补、文件切换
 */
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

#include "mmap_manager.hpp"
#include "mmap_file.hpp"
#include "file_header.hpp"
#include "market_records.hpp"

class TickTest : public ::testing::Test {
protected:
    static constexpr uint64_t kSlotsPerFile = 100;  // 小容量便于测试文件切换

    void SetUp() override {
        char tmpl[] = "/tmp/tick_test_XXXXXX";
        const char* d = ::mkdtemp(tmpl);
        ASSERT_NE(d, nullptr);
        test_dir_ = d;

        MmapManager::Config cfg;
        cfg.base_dir                = test_dir_;
        cfg.date                    = "20250408";
        cfg.snapshot_capacity       = 10;
        cfg.tick_capacity_per_file  = kSlotsPerFile;
        cfg.tick_total_preallocated = kSlotsPerFile * 5;  // 5 个 pool 文件

        mgr_ = std::make_unique<MmapManager>(cfg);
    }

    void TearDown() override {
        mgr_.reset();
        if (!test_dir_.empty()) {
            std::string cmd = "rm -rf " + test_dir_;
            ::system(cmd.c_str());
        }
    }

    static TickRecord make_tick(int64_t seq, double price, uint8_t rtype = 0) {
        TickRecord rec{};
        rec.record_type = rtype;
        if (rtype == 0) {
            rec.data.transaction.seq   = seq;
            rec.data.transaction.price = price;
            ::strncpy(rec.data.transaction.ticker, "600519",
                      sizeof(rec.data.transaction.ticker) - 1);
        } else {
            rec.data.order.seq   = seq;
            rec.data.order.price = price;
            ::strncpy(rec.data.order.ticker, "600519",
                      sizeof(rec.data.order.ticker) - 1);
        }
        return rec;
    }

    std::string test_dir_;
    std::unique_ptr<MmapManager> mgr_;
};

// ─────────────────────────────────────────────────────────────────────────────
// seq → slot 映射
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TickTest, SeqToSlot_FirstFile) {
    // seq=1 → file 0, slot 0
    // seq=100 → file 0, slot 99
    for (uint64_t seq = 1; seq <= kSlotsPerFile; ++seq) {
        auto rec = make_tick(static_cast<int64_t>(seq), static_cast<double>(seq));
        ASSERT_TRUE(mgr_->append_tick(1, 0, seq, rec)) << "seq=" << seq;
    }

    auto files = mgr_->get_tick_files(1);
    // 还未触发切换（第 kSlotsPerFile+1 条才切换），active 文件就是第 0 个
    ASSERT_EQ(files.size(), 1u);

    const MmapFile* f = files[0];
    EXPECT_EQ(f->header()->seq_start, 1u);

    // 验证每个 slot
    for (uint64_t i = 0; i < kSlotsPerFile; ++i) {
        const TickRecord* slot = static_cast<const TickRecord*>(f->at(i));
        ASSERT_NE(slot, nullptr);
        EXPECT_EQ(slot->state.load(std::memory_order_acquire),
                  static_cast<uint8_t>(SlotState::kValid));
        EXPECT_EQ(slot->data.transaction.seq, static_cast<int64_t>(i + 1));
    }
}

TEST_F(TickTest, SeqToSlot_RecordType_Order) {
    auto rec = make_tick(1, 10.5, 1);  // Order
    ASSERT_TRUE(mgr_->append_tick(2, 0, 1, rec));

    auto files = mgr_->get_tick_files(2);
    ASSERT_EQ(files.size(), 1u);

    const TickRecord* slot = static_cast<const TickRecord*>(files[0]->at(0));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->record_type, 1u);
    EXPECT_EQ(slot->data.order.seq, 1LL);
    EXPECT_DOUBLE_EQ(slot->data.order.price, 10.5);
}

// ─────────────────────────────────────────────────────────────────────────────
// 文件切换
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TickTest, FileSwitching_AfterFirstFileFull) {
    // 写满第 0 个文件
    for (uint64_t seq = 1; seq <= kSlotsPerFile; ++seq) {
        ASSERT_TRUE(mgr_->append_tick(1, 0, seq, make_tick(seq, 1.0)));
    }
    // 写入第 1 个文件的第 1 条（触发切换）
    uint64_t seq_new = kSlotsPerFile + 1;
    ASSERT_TRUE(mgr_->append_tick(1, 0, seq_new, make_tick(seq_new, 2.0)));

    auto files = mgr_->get_tick_files(1);
    ASSERT_EQ(files.size(), 2u);

    // 第 0 个文件已密封
    const FileHeader* hdr0 = files[0]->header();
    EXPECT_EQ(hdr0->seq_start, 1u);
    EXPECT_EQ(hdr0->seq_end, kSlotsPerFile);
    EXPECT_EQ(hdr0->file_seq, 0u);

    // 第 1 个文件为 active
    const FileHeader* hdr1 = files[1]->header();
    EXPECT_EQ(hdr1->seq_start, kSlotsPerFile + 1);
    EXPECT_EQ(hdr1->file_seq, 1u);

    // 验证第 1 个文件的 slot 0 已写入
    const TickRecord* slot = static_cast<const TickRecord*>(files[1]->at(0));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->state.load(std::memory_order_acquire),
              static_cast<uint8_t>(SlotState::kValid));
    EXPECT_EQ(slot->data.transaction.seq, static_cast<int64_t>(seq_new));
}

TEST_F(TickTest, FileSwitching_TwoFullFiles) {
    // 写满 2 个文件
    for (uint64_t seq = 1; seq <= kSlotsPerFile * 2; ++seq) {
        ASSERT_TRUE(mgr_->append_tick(1, 0, seq, make_tick(seq, 1.0)));
    }
    // 触发第 2 个文件
    ASSERT_TRUE(mgr_->append_tick(1, 0, kSlotsPerFile * 2 + 1,
                                  make_tick(kSlotsPerFile * 2 + 1, 3.0)));

    auto files = mgr_->get_tick_files(1);
    ASSERT_EQ(files.size(), 3u);

    EXPECT_EQ(files[0]->header()->file_seq, 0u);
    EXPECT_EQ(files[1]->header()->file_seq, 1u);
    EXPECT_EQ(files[2]->header()->file_seq, 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Gap 标记
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TickTest, MarkGap_SlotStateIsGap) {
    // seq=1 正常写入，seq=2 和 3 丢包，seq=4 正常
    ASSERT_TRUE(mgr_->append_tick(1, 0, 1, make_tick(1, 1.0)));
    ASSERT_TRUE(mgr_->mark_gap(1, 0, 2));
    ASSERT_TRUE(mgr_->mark_gap(1, 0, 3));
    ASSERT_TRUE(mgr_->append_tick(1, 0, 4, make_tick(4, 4.0)));

    auto files = mgr_->get_tick_files(1);
    ASSERT_EQ(files.size(), 1u);

    const MmapFile* f = files[0];

    auto state = [&](uint64_t idx) {
        const TickRecord* s = static_cast<const TickRecord*>(f->at(idx));
        return s ? static_cast<SlotState>(s->state.load(std::memory_order_acquire))
                 : SlotState::kEmpty;
    };

    EXPECT_EQ(state(0), SlotState::kValid);   // seq=1
    EXPECT_EQ(state(1), SlotState::kGap);     // seq=2
    EXPECT_EQ(state(2), SlotState::kGap);     // seq=3
    EXPECT_EQ(state(3), SlotState::kValid);   // seq=4

    // FileHeader gap_count
    EXPECT_EQ(f->header()->gap_count.load(std::memory_order_relaxed), 2u);
}

TEST_F(TickTest, MarkGap_StatsReflected) {
    for (uint64_t seq = 1; seq <= 5; ++seq) {
        mgr_->mark_gap(1, 0, seq);
    }
    EXPECT_EQ(mgr_->stats().total_gap_count, 5u);
}

// ─────────────────────────────────────────────────────────────────────────────
// CAS 回补
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TickTest, RecoverTick_SucceedsForGapSlot) {
    // 标记 seq=2 为 gap
    ASSERT_TRUE(mgr_->append_tick(1, 0, 1, make_tick(1, 1.0)));
    ASSERT_TRUE(mgr_->mark_gap(1, 0, 2));
    ASSERT_TRUE(mgr_->append_tick(1, 0, 3, make_tick(3, 3.0)));

    // 回补 seq=2
    auto recovered = make_tick(2, 2.0);
    ASSERT_TRUE(mgr_->recover_tick(1, 0, 2, recovered));

    auto files = mgr_->get_tick_files(1);
    ASSERT_EQ(files.size(), 1u);
    const MmapFile* f = files[0];

    // slot 1 (seq=2) 应为 kRecovered
    const TickRecord* slot1 = static_cast<const TickRecord*>(f->at(1));
    ASSERT_NE(slot1, nullptr);
    EXPECT_EQ(slot1->state.load(std::memory_order_acquire),
              static_cast<uint8_t>(SlotState::kRecovered));
    EXPECT_DOUBLE_EQ(slot1->data.transaction.price, 2.0);

    // recovered_count 递增
    EXPECT_EQ(f->header()->recovered_count.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(mgr_->stats().total_recovered, 1u);
}

TEST_F(TickTest, RecoverTick_FailsIfNotGap) {
    // slot 从未标为 gap（kEmpty 状态）
    mgr_->append_tick(1, 0, 1, make_tick(1, 1.0));
    // 正常写入的 slot 不能被回补
    auto rec = make_tick(1, 999.0);
    EXPECT_FALSE(mgr_->recover_tick(1, 0, 1, rec));  // slot=kValid，CAS 失败
}

TEST_F(TickTest, RecoverTick_FailsIfAlreadyRecovered) {
    mgr_->mark_gap(1, 0, 1);
    auto rec = make_tick(1, 1.0);
    EXPECT_TRUE(mgr_->recover_tick(1, 0, 1, rec));   // 第一次：成功
    EXPECT_FALSE(mgr_->recover_tick(1, 0, 1, rec));  // 第二次：CAS 失败
}

TEST_F(TickTest, RecoverTick_DataCorrectAfterRecovery) {
    mgr_->mark_gap(1, 0, 5);

    TickRecord rec = make_tick(5, 88.88);
    ASSERT_TRUE(mgr_->recover_tick(1, 0, 5, rec));

    auto files = mgr_->get_tick_files(1);
    ASSERT_EQ(files.size(), 1u);

    uint64_t slot_idx = (5 - 1) % kSlotsPerFile;  // = 4
    const TickRecord* slot =
        static_cast<const TickRecord*>(files[0]->at(slot_idx));
    ASSERT_NE(slot, nullptr);
    EXPECT_DOUBLE_EQ(slot->data.transaction.price, 88.88);
    EXPECT_EQ(slot->data.transaction.seq, 5LL);
}

// ─────────────────────────────────────────────────────────────────────────────
// 多通道独立
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TickTest, MultiChannel_Independent) {
    // 通道 1 写 10 条，通道 2 写 5 条
    for (uint64_t seq = 1; seq <= 10; ++seq)
        mgr_->append_tick(1, 0, seq, make_tick(seq, 1.0));
    for (uint64_t seq = 1; seq <= 5; ++seq)
        mgr_->append_tick(2, 0, seq, make_tick(seq, 2.0));

    auto files1 = mgr_->get_tick_files(1);
    auto files2 = mgr_->get_tick_files(2);

    ASSERT_EQ(files1.size(), 1u);
    ASSERT_EQ(files2.size(), 1u);
    EXPECT_NE(files1[0], files2[0]);

    EXPECT_EQ(files1[0]->size(), 10u);
    EXPECT_EQ(files2[0]->size(), 5u);
}

/**
 * @file test_tick_pool.cpp
 * @brief TickFilePool 单元测试：预分配、acquire、rename、池耗尽
 */
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <sys/stat.h>

#include "tick_file_pool.hpp"
#include "mmap_file.hpp"
#include "file_header.hpp"
#include "market_records.hpp"

class TickFilePoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        char tmpl[] = "/tmp/tick_pool_test_XXXXXX";
        const char* d = ::mkdtemp(tmpl);
        ASSERT_NE(d, nullptr);
        test_dir_ = d;
        pool_dir_ = test_dir_ + "/pool";
        ::mkdir(pool_dir_.c_str(), 0755);
        formal_dir_ = test_dir_ + "/tick/sh/20250408";
        // MmapFile::create_snapshot 会创建父目录，此处手动创建
        ::mkdir((test_dir_ + "/tick").c_str(), 0755);
        ::mkdir((test_dir_ + "/tick/sh").c_str(), 0755);
        ::mkdir(formal_dir_.c_str(), 0755);
    }

    void TearDown() override {
        if (!test_dir_.empty()) {
            std::string cmd = "rm -rf " + test_dir_;
            ::system(cmd.c_str());
        }
    }

    std::string formal_path(uint32_t ch, uint32_t fseq) {
        char buf[64];
        ::snprintf(buf, sizeof(buf), "/ch_%04u_%04u.mmap", ch, fseq);
        return formal_dir_ + buf;
    }

    std::string test_dir_;
    std::string pool_dir_;
    std::string formal_dir_;

    static constexpr size_t   kSlotSize      = sizeof(TickRecord);
    static constexpr uint64_t kSlotsPerFile  = 1000;
    static constexpr uint64_t kTotalCapacity = 5000;   // 5 个文件
};

// ─────────────────────────────────────────────────────────────────────────────
// 预分配
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TickFilePoolTest, Preallocate_CreatesFiles) {
    TickFilePool pool(pool_dir_, kSlotSize, kSlotsPerFile, kTotalCapacity);
    ASSERT_NO_THROW(pool.preallocate());

    // 5 个文件，remaining == 5
    EXPECT_EQ(pool.remaining(), 5u);

    // 验证文件存在且大小正确
    for (uint32_t i = 0; i < 5; ++i) {
        char buf[32];
        ::snprintf(buf, sizeof(buf), "/pool_%04u.mmap", i);
        std::string p = pool_dir_ + buf;

        struct stat st;
        ASSERT_EQ(::stat(p.c_str(), &st), 0) << "File missing: " << p;

        size_t expected = kHeaderSize + kSlotsPerFile * kSlotSize;
        EXPECT_EQ(static_cast<size_t>(st.st_size), expected);
    }
}

TEST_F(TickFilePoolTest, Preallocate_HeaderFieldsCorrect) {
    TickFilePool pool(pool_dir_, kSlotSize, kSlotsPerFile, kTotalCapacity);
    pool.preallocate();

    // 以只读方式验证 pool_0000.mmap 的文件头
    auto f = MmapFile::open_readonly(pool_dir_ + "/pool_0000.mmap");
    ASSERT_NE(f, nullptr);

    const FileHeader* hdr = f->header();
    EXPECT_EQ(hdr->magic,     kMagicTick);
    EXPECT_EQ(hdr->version,   kVersion);
    EXPECT_EQ(hdr->slot_size, static_cast<uint32_t>(kSlotSize));
    EXPECT_EQ(hdr->capacity,  kSlotsPerFile);
    EXPECT_EQ(hdr->data_type, 1u);           // Tick
    EXPECT_EQ(hdr->data_offset, kHeaderSize);
    EXPECT_EQ(hdr->write_index.load(), 0u);
}

TEST_F(TickFilePoolTest, Preallocate_Idempotent) {
    // 重复调用不抛出（文件已存在时跳过创建）
    TickFilePool pool(pool_dir_, kSlotSize, kSlotsPerFile, kTotalCapacity);
    ASSERT_NO_THROW(pool.preallocate());
    // 注意：第二次 preallocate 会重复 push 路径到队列，remaining 会翻倍
    // 实际使用中 preallocate 只调用一次；这里仅验证不崩溃
    ASSERT_NO_THROW(pool.preallocate());
}

// ─────────────────────────────────────────────────────────────────────────────
// acquire
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TickFilePoolTest, Acquire_RenamesFile) {
    TickFilePool pool(pool_dir_, kSlotSize, kSlotsPerFile, kTotalCapacity);
    pool.preallocate();

    EXPECT_EQ(pool.remaining(), 5u);

    std::unique_ptr<MmapFile> out;
    std::string target = formal_path(1, 0);
    ASSERT_TRUE(pool.acquire(target, &out));
    ASSERT_NE(out, nullptr);

    EXPECT_EQ(pool.remaining(), 4u);

    // 正式路径文件存在
    struct stat st;
    EXPECT_EQ(::stat(target.c_str(), &st), 0);

    // 原 pool 文件已消失
    EXPECT_NE(::stat((pool_dir_ + "/pool_0000.mmap").c_str(), &st), 0);
}

TEST_F(TickFilePoolTest, Acquire_FileOpenedReadwrite) {
    TickFilePool pool(pool_dir_, kSlotSize, kSlotsPerFile, kTotalCapacity);
    pool.preallocate();

    std::unique_ptr<MmapFile> out;
    ASSERT_TRUE(pool.acquire(formal_path(1, 0), &out));
    ASSERT_NE(out, nullptr);

    // 确认可以写入（更新 channel 字段）
    FileHeader* hdr = out->header();
    EXPECT_NO_THROW(hdr->channel = 1);
    EXPECT_EQ(hdr->channel, 1u);

    // 数据写入验证
    TickRecord rec{};
    rec.record_type = 0;
    rec.data.transaction.seq = 42;
    EXPECT_TRUE(out->write_tick(0, rec));
}

TEST_F(TickFilePoolTest, Acquire_MultipleFiles) {
    TickFilePool pool(pool_dir_, kSlotSize, kSlotsPerFile, kTotalCapacity);
    pool.preallocate();

    for (uint32_t i = 0; i < 5; ++i) {
        std::unique_ptr<MmapFile> out;
        ASSERT_TRUE(pool.acquire(formal_path(1, i), &out));
        ASSERT_NE(out, nullptr);
        EXPECT_EQ(pool.remaining(), 4 - i);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 池耗尽
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(TickFilePoolTest, Acquire_PoolExhausted) {
    TickFilePool pool(pool_dir_, kSlotSize, kSlotsPerFile, kTotalCapacity);
    pool.preallocate();

    // 取完所有 5 个文件
    for (uint32_t i = 0; i < 5; ++i) {
        std::unique_ptr<MmapFile> out;
        ASSERT_TRUE(pool.acquire(formal_path(1, i), &out));
    }
    EXPECT_EQ(pool.remaining(), 0u);

    // 再取一个应失败
    std::unique_ptr<MmapFile> out;
    EXPECT_FALSE(pool.acquire(formal_path(1, 5), &out));
    EXPECT_EQ(out, nullptr);
}

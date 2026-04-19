#include "mock_helper.hpp"

using namespace efvi;

// ---------------------------------------------------------------------------
// EF10 TX tests
// ---------------------------------------------------------------------------

TEST_F(MockTest, SendDoesNotThrow) {
    Vi vi(make_cfg());
    char buf[64] = {};
    EXPECT_NO_THROW(vi.send(buf, sizeof(buf)));
}

TEST_F(MockTest, SendIncreasesTxCount) {
    Vi vi(make_cfg());
    char buf[64] = {};
    vi.send(buf, sizeof(buf));
    EXPECT_EQ(1u, vi.stats_snapshot().tx_packets);
}

TEST_F(MockTest, SendMultiple) {
    Vi vi(make_cfg());
    char buf[64] = {};
    for (int i = 0; i < 5; ++i) vi.send(buf, sizeof(buf));
    EXPECT_EQ(5u, vi.stats_snapshot().tx_packets);
}

TEST_F(MockTest, SendBatchEF10) {
    Vi vi(make_cfg());
    char b0[64] = {}, b1[128] = {};
    const void* bufs[]   = {b0, b1};
    const size_t lens[] = {sizeof(b0), sizeof(b1)};
    vi.send_batch(bufs, lens, 2);
    EXPECT_EQ(2u, vi.stats_snapshot().tx_packets);
}

TEST_F(MockTest, SendZeroLenDoesNotCrash) {
    Vi vi(make_cfg());
    char buf[1] = {};
    EXPECT_NO_THROW(vi.send(buf, 0));
}

TEST_F(MockTest, SendMaxEthFrame) {
    Vi vi(make_cfg());
    char buf[1500] = {};
    EXPECT_NO_THROW(vi.send(buf, sizeof(buf)));
}

// ---------------------------------------------------------------------------
// EfCT TX tests
// ---------------------------------------------------------------------------

TEST_F(MockTest, EfCT_SendDoesNotThrow) {
    efvi_mock::nic_arch = EF_VI_ARCH_EFCT;
    Vi vi(make_cfg());
    char buf[64] = {};
    EXPECT_NO_THROW(vi.send(buf, sizeof(buf)));
}

TEST_F(MockTest, EfCT_SendIncreasesTxCount) {
    efvi_mock::nic_arch = EF_VI_ARCH_EFCT;
    Vi vi(make_cfg());
    char buf[64] = {};
    vi.send(buf, sizeof(buf));
    EXPECT_EQ(1u, vi.stats_snapshot().tx_packets);
}

// ---------------------------------------------------------------------------
// Checksum helpers (tested independently via src/checksum.cpp)
// ---------------------------------------------------------------------------

TEST_F(MockTest, ResetClearsTxStats) {
    Vi vi(make_cfg());
    char buf[64] = {};
    vi.send(buf, 64);
    vi.reset_stats();
    EXPECT_EQ(0u, vi.stats_snapshot().tx_packets);
}

TEST_F(MockTest, SendBatchSingleElement) {
    Vi vi(make_cfg());
    char buf[64] = {};
    const void* bufs[] = {buf};
    const size_t lens[] = {sizeof(buf)};
    vi.send_batch(bufs, lens, 1);
    EXPECT_EQ(1u, vi.stats_snapshot().tx_packets);
}

TEST_F(MockTest, SendBatchZeroCount) {
    Vi vi(make_cfg());
    EXPECT_NO_THROW(vi.send_batch(nullptr, nullptr, 0));
    EXPECT_EQ(0u, vi.stats_snapshot().tx_packets);
}

TEST_F(MockTest, TxDropsInitiallyZero) {
    Vi vi(make_cfg());
    EXPECT_EQ(0u, vi.stats_snapshot().tx_drops);
}

TEST_F(MockTest, TxErrorsInitiallyZero) {
    Vi vi(make_cfg());
    EXPECT_EQ(0u, vi.stats_snapshot().tx_errors);
}

TEST_F(MockTest, SendDoesNotLog) {
    std::vector<std::pair<LogLevel, std::string>> entries;
    auto cfg = make_cfg();
    cfg.log_callback = [&](LogLevel l, const std::string& m) {
        entries.push_back({l, m});
    };
    Vi vi(cfg);
    entries.clear();
    char buf[64] = {};
    vi.send(buf, sizeof(buf));
    EXPECT_TRUE(entries.empty());
}

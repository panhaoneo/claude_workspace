#include "mock_helper.hpp"

using namespace efvi;

// ---------------------------------------------------------------------------
// EF10 RX tests
// ---------------------------------------------------------------------------

TEST_F(MockTest, PollEmptyReturnsZero) {
    Vi vi(make_cfg());
    RxBatch batch;
    EXPECT_EQ(0, vi.poll(batch));
    EXPECT_TRUE(batch.empty());
}

TEST_F(MockTest, PollOneRxEvent) {
    Vi vi(make_cfg());
    efvi_mock::push_rx_event(0, 64);
    RxBatch batch;
    int n = vi.poll(batch);
    EXPECT_EQ(1, n);
    EXPECT_EQ(1u, batch.size());
}

TEST_F(MockTest, PollMultipleRxEvents) {
    Vi vi(make_cfg());
    efvi_mock::push_rx_event(0, 64);
    efvi_mock::push_rx_event(1, 128);
    efvi_mock::push_rx_event(2, 200);
    RxBatch batch;
    int n = vi.poll(batch);
    EXPECT_EQ(3, n);
    EXPECT_EQ(3u, batch.size());
}

TEST_F(MockTest, PollIncrementsRxStats) {
    Vi vi(make_cfg());
    efvi_mock::push_rx_event(0, 64);
    RxBatch batch;
    vi.poll(batch);
    EXPECT_EQ(1u, vi.stats_snapshot().rx_packets);
}

TEST_F(MockTest, RxPacketHasCorrectLen) {
    Vi vi(make_cfg());
    efvi_mock::push_rx_event(0, 99);
    RxBatch batch;
    vi.poll(batch);
    ASSERT_EQ(1u, batch.size());
    EXPECT_EQ(99u, batch[0].len());
}

TEST_F(MockTest, PollClearsBatchFirst) {
    Vi vi(make_cfg());
    efvi_mock::push_rx_event(0, 64);
    RxBatch batch;
    vi.poll(batch);  // fills with 1 packet

    efvi_mock::push_rx_event(1, 64);
    vi.poll(batch);  // should clear then add 1 new packet
    EXPECT_EQ(1u, batch.size());
}

TEST_F(MockTest, PollDoesNotLog) {
    std::vector<std::pair<LogLevel, std::string>> log;
    auto cfg = make_cfg();
    cfg.log_callback = [&](LogLevel l, const std::string& m) { log.push_back({l,m}); };
    Vi vi(cfg);
    log.clear();
    RxBatch batch;
    vi.poll(batch);
    EXPECT_TRUE(log.empty());
}

TEST_F(MockTest, RxDropEventIncrementsDrop) {
    Vi vi(make_cfg());
    ef_event ev{};
    ev.generic.type = EF_EVENT_TYPE_RX_NO_DESC_TRUNC;
    efvi_mock::pending_events.push_back(ev);
    RxBatch batch;
    vi.poll(batch);
    EXPECT_EQ(1u, vi.stats_snapshot().rx_drops);
    EXPECT_EQ(0u, batch.size());
}

TEST_F(MockTest, TxEventDoesNotAddToBatch) {
    Vi vi(make_cfg());
    efvi_mock::push_tx_event(0);
    RxBatch batch;
    int n = vi.poll(batch);
    EXPECT_EQ(0, n);
    EXPECT_TRUE(batch.empty());
}

// ---------------------------------------------------------------------------
// EfCT (RX_REF) tests
// ---------------------------------------------------------------------------

TEST_F(MockTest, EfCT_PollRxRefEvent) {
    efvi_mock::nic_arch = EF_VI_ARCH_EFCT;
    Vi vi(make_cfg());

    static const char pkt_data[] = "hello efct";
    efvi_mock::push_rx_ref_event(42, pkt_data, sizeof(pkt_data), 1234567890ULL);
    RxBatch batch;
    int n = vi.poll(batch);
    EXPECT_EQ(1, n);
    ASSERT_EQ(1u, batch.size());
}

TEST_F(MockTest, EfCT_PacketRefHasData) {
    efvi_mock::nic_arch = EF_VI_ARCH_EFCT;
    Vi vi(make_cfg());

    static const char pkt[] = "efct packet";
    efvi_mock::push_rx_ref_event(1, pkt, sizeof(pkt), 0);
    RxBatch batch;
    vi.poll(batch);
    ASSERT_EQ(1u, batch.size());
    EXPECT_EQ(pkt, batch[0].data());
    EXPECT_EQ(sizeof(pkt), batch[0].len());
}

TEST_F(MockTest, EfCT_PacketRefTimestamp) {
    efvi_mock::nic_arch = EF_VI_ARCH_EFCT;
    Vi vi(make_cfg());

    static const char pkt[] = "ts_test";
    efvi_mock::push_rx_ref_event(7, pkt, sizeof(pkt), 999888777ULL);
    RxBatch batch;
    vi.poll(batch);
    ASSERT_EQ(1u, batch.size());
    EXPECT_EQ(999888777ULL, batch[0].timestamp_ns());
}

TEST_F(MockTest, EfCT_PacketRefReleasedOnDestruct) {
    efvi_mock::nic_arch = EF_VI_ARCH_EFCT;
    Vi vi(make_cfg());

    static const char pkt[] = "raii_test";
    efvi_mock::push_rx_ref_event(99, pkt, sizeof(pkt), 0);
    {
        RxBatch batch;
        vi.poll(batch);
        ASSERT_EQ(1u, batch.size());
        // When batch goes out of scope, PacketRef destructor releases pkt 99
    }
    // After release, the store should be empty
    EXPECT_TRUE(efvi_mock::rx_ref_store.empty());
}

TEST_F(MockTest, EfCT_PacketRefMoveDoesNotDoubleRelease) {
    efvi_mock::nic_arch = EF_VI_ARCH_EFCT;
    Vi vi(make_cfg());

    static const char pkt[] = "move_test";
    efvi_mock::push_rx_ref_event(55, pkt, sizeof(pkt), 0);
    RxBatch batch;
    vi.poll(batch);
    ASSERT_EQ(1u, batch.size());

    PacketRef moved = std::move(batch[0]);
    // After move, original has no ownership
    EXPECT_EQ(pkt, moved.data());
    // rx_ref_store still has entry (not yet released)
    EXPECT_FALSE(efvi_mock::rx_ref_store.empty());
}

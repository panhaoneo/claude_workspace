#include "mock_helper.hpp"
#include <thread>
#include <vector>

using namespace efvi;

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST_F(MockTest, StatsInitiallyZero) {
    Vi vi(make_cfg());
    auto s = vi.stats_snapshot();
    EXPECT_EQ(0u, s.rx_packets);
    EXPECT_EQ(0u, s.tx_packets);
    EXPECT_EQ(0u, s.rx_drops);
    EXPECT_EQ(0u, s.tx_drops);
    EXPECT_EQ(0u, s.rx_errors);
    EXPECT_EQ(0u, s.tx_errors);
}

// ---------------------------------------------------------------------------
// Counter increments
// ---------------------------------------------------------------------------

TEST_F(MockTest, RxPacketsIncrement) {
    Vi vi(make_cfg());
    efvi_mock::push_rx_event(0, 64);
    RxBatch b;
    vi.poll(b);
    EXPECT_EQ(1u, vi.stats_snapshot().rx_packets);
}

TEST_F(MockTest, TxPacketsIncrement) {
    Vi vi(make_cfg());
    char buf[64] = {};
    vi.send(buf, 64);
    EXPECT_EQ(1u, vi.stats_snapshot().tx_packets);
}

TEST_F(MockTest, RxDropsIncrement) {
    Vi vi(make_cfg());
    ef_event ev{};
    ev.generic.type = EF_EVENT_TYPE_RX_DISCARD;
    efvi_mock::pending_events.push_back(ev);
    RxBatch b;
    vi.poll(b);
    EXPECT_EQ(1u, vi.stats_snapshot().rx_drops);
}

// ---------------------------------------------------------------------------
// Snapshot accuracy
// ---------------------------------------------------------------------------

TEST_F(MockTest, SnapshotAfterMultiplePolls) {
    Vi vi(make_cfg());
    for (int i = 0; i < 5; ++i) efvi_mock::push_rx_event(i % 512, 64);
    RxBatch b;
    while (vi.poll(b) > 0) {}
    EXPECT_EQ(5u, vi.stats_snapshot().rx_packets);
}

TEST_F(MockTest, SnapshotReflectsTxAndRx) {
    Vi vi(make_cfg());
    efvi_mock::push_rx_event(0, 64);
    RxBatch b;
    vi.poll(b);
    char buf[64] = {};
    vi.send(buf, 64);
    auto s = vi.stats_snapshot();
    EXPECT_EQ(1u, s.rx_packets);
    EXPECT_EQ(1u, s.tx_packets);
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST_F(MockTest, ResetClearsAllCounters) {
    Vi vi(make_cfg());
    efvi_mock::push_rx_event(0, 64);
    RxBatch b;
    vi.poll(b);
    char buf[64] = {};
    vi.send(buf, 64);

    vi.reset_stats();
    auto s = vi.stats_snapshot();
    EXPECT_EQ(0u, s.rx_packets);
    EXPECT_EQ(0u, s.tx_packets);
    EXPECT_EQ(0u, s.rx_drops);
}

TEST_F(MockTest, ResetThenAccumulatesAgain) {
    Vi vi(make_cfg());
    char buf[64] = {};
    vi.send(buf, 64);
    vi.reset_stats();
    vi.send(buf, 64);
    EXPECT_EQ(1u, vi.stats_snapshot().tx_packets);
}

// ---------------------------------------------------------------------------
// Snapshot + operator
// ---------------------------------------------------------------------------

TEST_F(MockTest, SnapshotAddition) {
    Stats::Snapshot a, b;
    a.rx_packets = 10; a.tx_packets = 5;
    b.rx_packets = 3;  b.tx_packets = 7;
    auto c = a + b;
    EXPECT_EQ(13u, c.rx_packets);
    EXPECT_EQ(12u, c.tx_packets);
}

// ---------------------------------------------------------------------------
// Thread-safety of snapshot reads
// ---------------------------------------------------------------------------

TEST_F(MockTest, ConcurrentSnapshotReads) {
    Vi vi(make_cfg());
    char buf[64] = {};
    vi.send(buf, 64);

    std::vector<std::thread> threads;
    std::atomic<int> mismatches{0};
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            auto s = vi.stats_snapshot();
            if (s.tx_packets != 1u) mismatches.fetch_add(1);
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(0, mismatches.load());
}

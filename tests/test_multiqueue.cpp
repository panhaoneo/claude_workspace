#include "mock_helper.hpp"
#include <thread>
#include <vector>
#include <atomic>

using namespace efvi;

// ---------------------------------------------------------------------------
// ViSet construction
// ---------------------------------------------------------------------------

TEST_F(MockTest, ViSetSingleQueue) {
    ViSetConfig cfg;
    cfg.queue_count  = 1;
    cfg.base_cfg     = make_cfg();
    EXPECT_NO_THROW(ViSet vs(cfg));
}

TEST_F(MockTest, ViSetMultiQueue) {
    ViSetConfig cfg;
    cfg.queue_count = 4;
    cfg.base_cfg    = make_cfg();
    EXPECT_NO_THROW(ViSet vs(cfg));
}

TEST_F(MockTest, ViSetZeroQueuesThrows) {
    ViSetConfig cfg;
    cfg.queue_count = 0;
    cfg.base_cfg    = make_cfg();
    EXPECT_THROW(ViSet vs(cfg), ViException);
}

TEST_F(MockTest, ViSetQueueCount) {
    ViSetConfig cfg;
    cfg.queue_count = 3;
    cfg.base_cfg    = make_cfg();
    ViSet vs(cfg);
    EXPECT_EQ(3, vs.queue_count());
}

// ---------------------------------------------------------------------------
// Vi access
// ---------------------------------------------------------------------------

TEST_F(MockTest, ViSetGetVi) {
    ViSetConfig cfg;
    cfg.queue_count = 2;
    cfg.base_cfg    = make_cfg();
    ViSet vs(cfg);
    EXPECT_NO_THROW(vs.vi(0));
    EXPECT_NO_THROW(vs.vi(1));
}

TEST_F(MockTest, ViSetOutOfRangeThrows) {
    ViSetConfig cfg;
    cfg.queue_count = 2;
    cfg.base_cfg    = make_cfg();
    ViSet vs(cfg);
    EXPECT_THROW(vs.vi(2), ViException);
    EXPECT_THROW(vs.vi(-1), ViException);
}

TEST_F(MockTest, ViSetViHasCorrectArch) {
    efvi_mock::nic_arch = EF_VI_ARCH_EF10;
    ViSetConfig cfg;
    cfg.queue_count = 2;
    cfg.base_cfg    = make_cfg();
    ViSet vs(cfg);
    EXPECT_EQ(NicArch::EF10, vs.vi(0).arch());
    EXPECT_EQ(NicArch::EF10, vs.vi(1).arch());
}

// ---------------------------------------------------------------------------
// Per-queue config override
// ---------------------------------------------------------------------------

TEST_F(MockTest, ViSetPerQueueConfig) {
    ViSetConfig cfg;
    cfg.queue_count = 2;
    cfg.base_cfg    = make_cfg();
    cfg.per_queue_cfg.push_back(make_cfg("eth0"));
    cfg.per_queue_cfg.push_back(make_cfg("eth1"));
    EXPECT_NO_THROW(ViSet vs(cfg));
}

// ---------------------------------------------------------------------------
// Aggregate statistics
// ---------------------------------------------------------------------------

TEST_F(MockTest, AggregateStatsZeroInitially) {
    ViSetConfig cfg;
    cfg.queue_count = 2;
    cfg.base_cfg    = make_cfg();
    ViSet vs(cfg);
    auto s = vs.aggregate_stats();
    EXPECT_EQ(0u, s.rx_packets);
    EXPECT_EQ(0u, s.tx_packets);
}

TEST_F(MockTest, AggregateStatsSumAcrossQueues) {
    ViSetConfig cfg;
    cfg.queue_count = 2;
    cfg.base_cfg    = make_cfg();
    ViSet vs(cfg);

    char buf[64] = {};
    vs.vi(0).send(buf, 64);   // 1 tx on queue 0
    vs.vi(1).send(buf, 64);   // 1 tx on queue 1
    vs.vi(1).send(buf, 64);   // 2 tx on queue 1

    auto s = vs.aggregate_stats();
    EXPECT_EQ(3u, s.tx_packets);
}

TEST_F(MockTest, ResetAllStats) {
    ViSetConfig cfg;
    cfg.queue_count = 2;
    cfg.base_cfg    = make_cfg();
    ViSet vs(cfg);

    char buf[64] = {};
    vs.vi(0).send(buf, 64);
    vs.vi(1).send(buf, 64);
    vs.reset_all_stats();

    EXPECT_EQ(0u, vs.aggregate_stats().tx_packets);
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

TEST_F(MockTest, ViSetMoveConstructor) {
    ViSetConfig cfg;
    cfg.queue_count = 2;
    cfg.base_cfg    = make_cfg();
    ViSet a(cfg);
    ViSet b(std::move(a));
    EXPECT_EQ(2, b.queue_count());
}

// ---------------------------------------------------------------------------
// Concurrent poll across queues
// ---------------------------------------------------------------------------

TEST_F(MockTest, ConcurrentPollDifferentQueues) {
    ViSetConfig cfg;
    cfg.queue_count = 2;
    cfg.base_cfg    = make_cfg();
    ViSet vs(cfg);

    efvi_mock::push_rx_event(0, 64);
    efvi_mock::push_rx_event(0, 64);

    std::atomic<int> total{0};
    std::vector<std::thread> threads;
    for (int q = 0; q < 2; ++q) {
        threads.emplace_back([&vs, &total, q]() {
            RxBatch b;
            total.fetch_add(vs.vi(q).poll(b));
        });
    }
    for (auto& t : threads) t.join();
    // Both queues share the same mock event queue; total depends on race.
    // We just verify no crash and total <= 2.
    EXPECT_LE(total.load(), 2);
}

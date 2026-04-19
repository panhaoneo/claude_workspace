#include "mock_helper.hpp"
#include <thread>
#include <atomic>
#include <utility>

using namespace efvi;

// ---------------------------------------------------------------------------
// Basic construction / destruction
// ---------------------------------------------------------------------------

TEST_F(MockTest, ConstructWithValidConfig) {
    EXPECT_NO_THROW(Vi vi(make_cfg()));
}

TEST_F(MockTest, EmptyInterfaceThrows) {
    ViConfig cfg;
    cfg.interface = "";
    EXPECT_THROW(Vi vi(cfg), ViException);
}

TEST_F(MockTest, InvalidRxqDepthThrows_NotPow2) {
    auto cfg = make_cfg();
    cfg.perf.rxq_depth = 500;   // not power-of-2
    EXPECT_THROW(Vi vi(cfg), ViException);
}

TEST_F(MockTest, InvalidRxqDepthThrows_TooSmall) {
    auto cfg = make_cfg();
    cfg.perf.rxq_depth = 32;
    EXPECT_THROW(Vi vi(cfg), ViException);
}

TEST_F(MockTest, InvalidTxqDepthThrows) {
    auto cfg = make_cfg();
    cfg.perf.txq_depth = 100;   // not power-of-2
    EXPECT_THROW(Vi vi(cfg), ViException);
}

TEST_F(MockTest, InvalidRefillBatchThrows) {
    auto cfg = make_cfg();
    cfg.perf.rx_refill_batch = 7;
    EXPECT_THROW(Vi vi(cfg), ViException);
}

TEST_F(MockTest, InvalidCtpioThresholdThrows) {
    auto cfg = make_cfg();
    cfg.perf.ctpio_threshold = 2000;
    EXPECT_THROW(Vi vi(cfg), ViException);
}

// ---------------------------------------------------------------------------
// Failure injection
// ---------------------------------------------------------------------------

TEST_F(MockTest, DriverOpenFailureThrows) {
    efvi_mock::fail_driver_open = true;
    EXPECT_THROW(Vi vi(make_cfg()), ViException);
}

TEST_F(MockTest, PdAllocFailureThrows) {
    efvi_mock::fail_pd_alloc = true;
    EXPECT_THROW(Vi vi(make_cfg()), ViException);
}

TEST_F(MockTest, ViAllocFailureThrows) {
    efvi_mock::fail_vi_alloc = true;
    EXPECT_THROW(Vi vi(make_cfg()), ViException);
}

TEST_F(MockTest, MemregAllocFailureThrows) {
    efvi_mock::fail_memreg_alloc = true;
    EXPECT_THROW(Vi vi(make_cfg()), ViException);
}

// ---------------------------------------------------------------------------
// ViException properties
// ---------------------------------------------------------------------------

TEST_F(MockTest, ExceptionHasErrorCode) {
    efvi_mock::fail_driver_open = true;
    try {
        Vi vi(make_cfg());
        FAIL() << "expected ViException";
    } catch (const ViException& e) {
        EXPECT_NE(0, e.error_code());
    }
}

TEST_F(MockTest, ExceptionMessageNonEmpty) {
    efvi_mock::fail_pd_alloc = true;
    try {
        Vi vi(make_cfg());
        FAIL() << "expected ViException";
    } catch (const ViException& e) {
        EXPECT_FALSE(std::string(e.what()).empty());
    }
}

TEST_F(MockTest, ExceptionMessageContainsCode) {
    efvi_mock::fail_driver_open = true;
    try {
        Vi vi(make_cfg());
        FAIL() << "expected ViException";
    } catch (const ViException& e) {
        std::string msg(e.what());
        // Message should contain error code in brackets
        EXPECT_NE(std::string::npos, msg.find('['));
    }
}

// ---------------------------------------------------------------------------
// NIC architecture detection
// ---------------------------------------------------------------------------

TEST_F(MockTest, ArchIsEF10ByDefault) {
    efvi_mock::nic_arch = EF_VI_ARCH_EF10;
    Vi vi(make_cfg());
    EXPECT_EQ(NicArch::EF10, vi.arch());
}

TEST_F(MockTest, ArchIsEfCTWhenMockSetToEfCT) {
    efvi_mock::nic_arch = EF_VI_ARCH_EFCT;
    Vi vi(make_cfg());
    EXPECT_EQ(NicArch::EFCT, vi.arch());
}

TEST_F(MockTest, EfCTRejectsPhysMode) {
    efvi_mock::nic_arch = EF_VI_ARCH_EFCT;
    auto cfg = make_cfg();
    cfg.perf.pd_flags = EF_PD_PHYS_MODE;
    EXPECT_THROW(Vi vi(cfg), ViException);
}

// ---------------------------------------------------------------------------
// NicInfo
// ---------------------------------------------------------------------------

TEST_F(MockTest, NicInfoModelEF10) {
    efvi_mock::nic_arch = EF_VI_ARCH_EF10;
    Vi vi(make_cfg());
    EXPECT_EQ("X2522", vi.nic_info().model);
}

TEST_F(MockTest, NicInfoModelEfCT) {
    efvi_mock::nic_arch = EF_VI_ARCH_EFCT;
    Vi vi(make_cfg());
    EXPECT_EQ("X3522", vi.nic_info().model);
}

TEST_F(MockTest, NicInfoDriverVersionNonEmpty) {
    Vi vi(make_cfg());
    EXPECT_FALSE(vi.nic_info().driver_version.empty());
}

TEST_F(MockTest, NicInfoMtuDefault) {
    Vi vi(make_cfg());
    EXPECT_EQ(1500u, vi.nic_info().mtu);
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

TEST_F(MockTest, MoveConstructor) {
    Vi original(make_cfg());
    Vi moved(std::move(original));
    EXPECT_EQ(NicArch::EF10, moved.arch());
}

TEST_F(MockTest, MoveAssignment) {
    Vi a(make_cfg());
    Vi b(make_cfg());
    b = std::move(a);
    EXPECT_EQ(NicArch::EF10, b.arch());
}

// ---------------------------------------------------------------------------
// Multiple concurrent instances
// ---------------------------------------------------------------------------

TEST_F(MockTest, MultipleViInstances) {
    Vi a(make_cfg("eth0"));
    Vi b(make_cfg("eth1"));
    EXPECT_EQ(NicArch::EF10, a.arch());
    EXPECT_EQ(NicArch::EF10, b.arch());
}

TEST_F(MockTest, ConcurrentConstruction) {
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            try {
                Vi vi(make_cfg());
            } catch (...) {
                errors.fetch_add(1);
            }
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(0, errors.load());
}

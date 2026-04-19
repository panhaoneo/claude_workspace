#pragma once
// Convenience header for all test files — includes mock control API
// and common helpers.
#include "efvi/vi.hpp"
#include "efvi/vi_set.hpp"
#include "efvi/config.hpp"
#include "efvi/exception.hpp"
#include "mock/mock_efvi.hpp"  // NOLINT — internal header exposed for tests

#include <gtest/gtest.h>
#include <string>
#include <vector>

// Build a minimal valid ViConfig with required fields set.
inline efvi::ViConfig make_cfg(const std::string& iface = "eth0") {
    efvi::ViConfig cfg;
    cfg.interface = iface;
    return cfg;
}

// Test fixture that resets mock state before each test.
class MockTest : public ::testing::Test {
protected:
    void SetUp() override    { efvi_mock::reset(); }
    void TearDown() override {}
};

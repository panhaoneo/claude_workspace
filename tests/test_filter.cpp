#include "mock_helper.hpp"

using namespace efvi;

// ---------------------------------------------------------------------------
// FilterSpec factory methods
// ---------------------------------------------------------------------------

TEST_F(MockTest, FilterSpecUdp) {
    auto fs = FilterSpec::udp(0x01020304, 8080);
    EXPECT_EQ(FilterType::UDP_LOCAL, fs.type);
    EXPECT_EQ(0x01020304u, fs.local_ip);
    EXPECT_EQ(8080, fs.local_port);
}

TEST_F(MockTest, FilterSpecTcp) {
    auto fs = FilterSpec::tcp(0, 443);
    EXPECT_EQ(FilterType::TCP_LOCAL, fs.type);
    EXPECT_EQ(443, fs.local_port);
}

TEST_F(MockTest, FilterSpecMulticastAll) {
    auto fs = FilterSpec::multicast_all();
    EXPECT_EQ(FilterType::MULTICAST_ALL, fs.type);
}

TEST_F(MockTest, FilterSpecMulticastIp) {
    auto fs = FilterSpec::multicast_ip(0xe0010101);
    EXPECT_EQ(FilterType::MULTICAST_IP, fs.type);
    EXPECT_EQ(0xe0010101u, fs.mcast_ip);
}

TEST_F(MockTest, FilterSpecMacVlan) {
    uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    auto fs = FilterSpec::mac_vlan(mac, 100);
    EXPECT_EQ(FilterType::MAC_VLAN, fs.type);
    EXPECT_EQ(100, fs.vlan_id);
    EXPECT_EQ(0x00, fs.mac[0]);
    EXPECT_EQ(0x55, fs.mac[5]);
}

// ---------------------------------------------------------------------------
// add / remove
// ---------------------------------------------------------------------------

TEST_F(MockTest, AddFilterReturnsNonZeroCookie) {
    Vi vi(make_cfg());
    FilterCookie c = vi.add_filter(FilterSpec::udp(0, 5000));
    EXPECT_NE(0, c);
}

TEST_F(MockTest, TwoFiltersHaveDifferentCookies) {
    Vi vi(make_cfg());
    FilterCookie a = vi.add_filter(FilterSpec::udp(0, 5000));
    FilterCookie b = vi.add_filter(FilterSpec::udp(0, 5001));
    EXPECT_NE(a, b);
}

TEST_F(MockTest, RemoveFilterDoesNotThrow) {
    Vi vi(make_cfg());
    FilterCookie c = vi.add_filter(FilterSpec::udp(0, 5000));
    EXPECT_NO_THROW(vi.remove_filter(c));
}

TEST_F(MockTest, RemoveUnknownCookieThrows) {
    Vi vi(make_cfg());
    EXPECT_THROW(vi.remove_filter(9999), ViException);
}

TEST_F(MockTest, AddFilterFailureThrows) {
    Vi vi(make_cfg());
    efvi_mock::fail_filter_add = true;
    EXPECT_THROW(vi.add_filter(FilterSpec::udp(0, 5000)), ViException);
}

// ---------------------------------------------------------------------------
// X3522 filter limit (EfCT)
// ---------------------------------------------------------------------------

TEST_F(MockTest, EfCT_FilterLimitEnforced) {
    efvi_mock::nic_arch = EF_VI_ARCH_EFCT;
    Vi vi(make_cfg());

    // Add 256 filters (max) — should succeed
    std::vector<FilterCookie> cookies;
    for (int i = 0; i < 256; ++i) {
        FilterCookie c = vi.add_filter(FilterSpec::udp(0, static_cast<uint16_t>(i + 1)));
        cookies.push_back(c);
    }

    // 257th filter must throw
    EXPECT_THROW(vi.add_filter(FilterSpec::udp(0, 9999)), ViException);

    // After removing one, adding another should work
    vi.remove_filter(cookies.back());
    cookies.pop_back();
    EXPECT_NO_THROW(vi.add_filter(FilterSpec::udp(0, 9999)));
}

TEST_F(MockTest, AddMultipleFilterTypes) {
    Vi vi(make_cfg());
    EXPECT_NO_THROW(vi.add_filter(FilterSpec::udp(0, 5000)));
    EXPECT_NO_THROW(vi.add_filter(FilterSpec::tcp(0, 443)));
    EXPECT_NO_THROW(vi.add_filter(FilterSpec::multicast_all()));
}

TEST_F(MockTest, ConfigFiltersInstalledDuringInit) {
    auto cfg = make_cfg();
    cfg.filters.push_back(FilterSpec::udp(0, 9000));
    cfg.filters.push_back(FilterSpec::multicast_all());
    // If construction succeeds, filters were installed
    EXPECT_NO_THROW(Vi vi(cfg));
}

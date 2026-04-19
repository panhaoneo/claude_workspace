#include "mock_efvi.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Mock state
// ---------------------------------------------------------------------------

namespace efvi_mock {

bool fail_driver_open  = false;
bool fail_pd_alloc     = false;
bool fail_vi_alloc     = false;
bool fail_memreg_alloc = false;
bool fail_pio_alloc    = false;
bool fail_filter_add   = false;
bool fail_vi_set_alloc = false;

int nic_arch = EF_VI_ARCH_EF10;

std::vector<ef_event> pending_events;
std::vector<std::pair<int,RxRefData>> rx_ref_store;

static int s_next_id = 1;

void reset() {
    fail_driver_open  = false;
    fail_pd_alloc     = false;
    fail_vi_alloc     = false;
    fail_memreg_alloc = false;
    fail_pio_alloc    = false;
    fail_filter_add   = false;
    fail_vi_set_alloc = false;
    nic_arch          = EF_VI_ARCH_EF10;
    pending_events.clear();
    rx_ref_store.clear();
    s_next_id = 1;
}

void push_rx_event(int buf_id, int len) {
    ef_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.rx.type    = EF_EVENT_TYPE_RX;
    ev.rx.desc_id = buf_id;
    ev.rx.len     = static_cast<unsigned>(len);
    pending_events.push_back(ev);
}

void push_tx_event(int buf_id) {
    ef_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.tx.type    = EF_EVENT_TYPE_TX;
    ev.tx.desc_id = buf_id;
    pending_events.push_back(ev);
}

void push_rx_ref_event(int pkt_id, const void* data, size_t len, uint64_t ts_ns) {
    rx_ref_store.push_back({pkt_id, {data, len, ts_ns}});

    ef_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.rx_ref.type         = EF_EVENT_TYPE_RX_REF;
    ev.rx_ref.pkt_id       = pkt_id;
    ev.rx_ref.len          = static_cast<unsigned>(len);
    ev.rx_ref.timestamp_ns = ts_ns;
    pending_events.push_back(ev);
}

} // namespace efvi_mock

// ---------------------------------------------------------------------------
// Function implementations
// ---------------------------------------------------------------------------

int ef_driver_open(ef_driver_handle* dh_out) {
    if (efvi_mock::fail_driver_open) return -ENODEV;
    *dh_out = efvi_mock::s_next_id++;
    return 0;
}

void ef_driver_close(ef_driver_handle /*dh*/) {}

int ef_pd_alloc(ef_pd* pd, ef_driver_handle /*dh*/, int /*ifindex*/, unsigned flags) {
    if (efvi_mock::fail_pd_alloc) return -ENOMEM;
    pd->id    = efvi_mock::s_next_id++;
    pd->flags = flags;
    return 0;
}

void ef_pd_free(ef_pd* /*pd*/, ef_driver_handle /*dh*/) {}

int ef_vi_alloc_from_pd(ef_vi* vi, ef_driver_handle /*vi_dh*/,
                         ef_pd* /*pd*/, ef_driver_handle /*pd_dh*/,
                         int /*evq_cap*/, int rxq_cap, int txq_cap,
                         ef_vi* /*evq_vi*/, int /*evq_flags*/, ef_vi_flags /*vi_flags*/) {
    if (efvi_mock::fail_vi_alloc) return -ENOMEM;
    vi->id       = efvi_mock::s_next_id++;
    vi->nic_arch = efvi_mock::nic_arch;
    vi->rxq_cap  = rxq_cap;
    vi->txq_cap  = txq_cap;
    return 0;
}

void ef_vi_free(ef_vi* /*vi*/, ef_driver_handle /*dh*/) {}

static ef_nic_type s_nic_type;

const ef_nic_type* ef_vi_nic_type(const ef_vi* vi) {
    s_nic_type.arch     = vi->nic_arch;
    s_nic_type.variant  = 'A';
    s_nic_type.revision = 0;
    return &s_nic_type;
}

int ef_memreg_alloc(ef_memreg* mr, ef_driver_handle /*mr_dh*/,
                    ef_pd* /*pd*/, ef_driver_handle /*pd_dh*/,
                    void* base, size_t size) {
    if (efvi_mock::fail_memreg_alloc) return -ENOMEM;
    mr->base = base;
    mr->size = size;
    return 0;
}

void ef_memreg_free(ef_memreg* /*mr*/, ef_driver_handle /*dh*/) {}

ef_addr ef_memreg_dma_addr(const ef_memreg* mr, size_t offset) {
    return static_cast<ef_addr>(
        reinterpret_cast<uintptr_t>(static_cast<char*>(mr->base) + offset));
}

int ef_pio_alloc(ef_pio* pio, ef_driver_handle /*dh*/, ef_pd* /*pd*/,
                 ef_driver_handle /*pd_dh*/, int /*pio_size*/) {
    if (efvi_mock::fail_pio_alloc) return -ENOMEM;
    pio->id = efvi_mock::s_next_id++;
    return 0;
}

void ef_pio_free(ef_pio* /*pio*/, ef_driver_handle /*dh*/) {}

int ef_pio_link_vi(ef_pio* /*pio*/, ef_driver_handle /*pio_dh*/,
                   ef_vi* /*vi*/, ef_driver_handle /*vi_dh*/) {
    return 0;
}

int ef_vi_receive_post(ef_vi* /*vi*/, ef_addr /*addr*/, int /*id*/) {
    return 0;
}

int ef_eventq_poll(ef_vi* /*vi*/, ef_event* evs, int evs_len) {
    auto& q = efvi_mock::pending_events;
    if (q.empty()) return 0;
    int n = static_cast<int>(q.size()) < evs_len ? static_cast<int>(q.size()) : evs_len;
    for (int i = 0; i < n; ++i) evs[i] = q[i];
    q.erase(q.begin(), q.begin() + n);
    return n;
}

int ef_vi_receive_get_timestamp_sync(const ef_vi* /*vi*/, const void* /*pkt*/,
                                     ef_timespec* ts_out, unsigned* flags_out) {
    if (ts_out) { ts_out->tv_sec = 0; ts_out->tv_nsec = 0; }
    if (flags_out) *flags_out = EF_VI_SYNC_FLAG_CLOCK_IN_SYNC;
    return 0;
}

void ef_filter_spec_init(ef_filter_spec* fs, ef_filter_flags /*flags*/) {
    memset(fs, 0, sizeof(*fs));
    fs->type = -1;
}

int ef_filter_spec_set_ip4_local(ef_filter_spec* fs, int proto,
                                  unsigned ip, int port) {
    fs->type  = EF_FILTER_SPEC_TYPE_IP4_LOCAL;
    fs->proto = proto;
    fs->ip    = ip;
    fs->port  = static_cast<unsigned short>(port);
    return 0;
}

int ef_filter_spec_set_multicast_all(ef_filter_spec* fs) {
    fs->type = EF_FILTER_SPEC_TYPE_MCAST_ALL;
    return 0;
}

int ef_filter_spec_set_multicast_mismatch(ef_filter_spec* fs) {
    fs->type = EF_FILTER_SPEC_TYPE_MCAST_ALL;
    return 0;
}

int ef_filter_spec_set_eth_local(ef_filter_spec* fs, int vlan_id,
                                  const uint8_t* mac) {
    fs->type   = EF_FILTER_SPEC_TYPE_MAC_VLAN;
    fs->vlan   = vlan_id;
    memcpy(fs->mac, mac, 6);
    return 0;
}

static int s_next_filter_id = 100;

int ef_vi_filter_add(ef_vi* /*vi*/, ef_driver_handle /*dh*/,
                     const ef_filter_spec* /*fs*/, ef_filter_cookie* cookie_out) {
    if (efvi_mock::fail_filter_add) return -EBUSY;
    cookie_out->id = s_next_filter_id++;
    return 0;
}

int ef_vi_filter_del(ef_vi* /*vi*/, ef_driver_handle /*dh*/,
                     ef_filter_cookie* /*cookie*/) {
    return 0;
}

int ef_vi_transmit_ctpio_copy(ef_vi* /*vi*/, const void* /*buf*/, size_t /*len*/,
                               int /*threshold*/) {
    return 0;
}

int ef_vi_transmit(ef_vi* /*vi*/, ef_addr /*dma_addr*/, int /*bytes*/, int /*dma_id*/) {
    return 0;
}

int ef_vi_transmit_pio(ef_vi* /*vi*/, int /*offset*/, int /*bytes*/, int /*dma_id*/) {
    return 0;
}

void ef_vi_transmit_push(ef_vi* /*vi*/) {}

int ef_vi_stats_query(const ef_vi* /*vi*/, ef_driver_handle /*dh*/,
                      void* data, int data_size) {
    if (data && data_size > 0) memset(data, 0, static_cast<size_t>(data_size));
    return 0;
}

int efct_vi_rxpkt_get(ef_vi* /*vi*/, int pkt_id, const void** base_out) {
    for (auto& p : efvi_mock::rx_ref_store) {
        if (p.first == pkt_id) {
            *base_out = p.second.ptr;
            return 0;
        }
    }
    *base_out = nullptr;
    return -ENOENT;
}

void efct_vi_rxpkt_release(ef_vi* /*vi*/, int pkt_id) {
    auto& store = efvi_mock::rx_ref_store;
    store.erase(std::remove_if(store.begin(), store.end(),
                               [pkt_id](const std::pair<int,efvi_mock::RxRefData>& p) {
                                   return p.first == pkt_id;
                               }),
                store.end());
}

int ef_vi_set_alloc_from_pd(ef_vi_set* vis, ef_driver_handle /*dh*/,
                             ef_pd* /*pd*/, ef_driver_handle /*pd_dh*/,
                             int /*ifindex*/, int /*n_vis*/,
                             ef_filter_cookie* /*cookie_out*/) {
    if (efvi_mock::fail_vi_set_alloc) return -ENOMEM;
    vis->id = efvi_mock::s_next_id++;
    return 0;
}

void ef_vi_set_free(ef_vi_set* /*vis*/, ef_driver_handle /*dh*/) {}

int ef_vi_alloc_from_set(ef_vi* vi, ef_driver_handle /*vi_dh*/,
                          ef_vi_set* /*vis*/, ef_driver_handle /*vis_dh*/,
                          int /*index*/, int evq_cap, int rxq_cap, int txq_cap,
                          ef_vi* evq_vi, int evq_flags, ef_vi_flags vi_flags) {
    return ef_vi_alloc_from_pd(vi, 0, nullptr, 0,
                                evq_cap, rxq_cap, txq_cap,
                                evq_vi, evq_flags, vi_flags);
}

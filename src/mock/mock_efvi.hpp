#pragma once
// Mock ef_vi types and API — compiled only when EFVI_MOCK_MODE is defined.
// Provides the same function signatures as real OpenOnload headers so that
// vi_impl.cpp compiles without any Onload installation.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Basic types
// ---------------------------------------------------------------------------

typedef int ef_driver_handle;

typedef struct { int id; unsigned flags; } ef_pd;

// Simplified ef_vi — real struct is much larger; we only need the fields
// that vi_impl.cpp reads directly.
typedef struct {
    int id;
    int nic_arch;  // EF_VI_ARCH_EF10 or EF_VI_ARCH_EFCT
    int rxq_cap;
    int txq_cap;
} ef_vi;

typedef struct { void* base; size_t size; } ef_memreg;
typedef struct { int id; } ef_pio;
typedef struct { int id; } ef_vi_set;

typedef uint64_t ef_addr;

// ---------------------------------------------------------------------------
// NIC type
// ---------------------------------------------------------------------------

#define EF_VI_ARCH_EF10  4
#define EF_VI_ARCH_EFCT  8

typedef struct {
    int  arch;
    char variant;
    int  revision;
} ef_nic_type;

// ---------------------------------------------------------------------------
// PD flags
// ---------------------------------------------------------------------------

#define EF_PD_DEFAULT   0u
#define EF_PD_VF        1u
#define EF_PD_PHYS_MODE 2u

// ---------------------------------------------------------------------------
// VI flags
// ---------------------------------------------------------------------------

typedef enum { EF_VI_FLAGS_DEFAULT = 0 } ef_vi_flags;

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------

typedef enum { EF_FILTER_FLAG_NONE = 0 } ef_filter_flags;

typedef struct {
    int      type;
    unsigned ip;
    unsigned short port;
    int      proto;
    uint8_t  mac[6];
    int      vlan;
    unsigned mcast_ip;
} ef_filter_spec;

typedef struct { int id; } ef_filter_cookie;

#define EF_FILTER_SPEC_TYPE_IP4_LOCAL   0
#define EF_FILTER_SPEC_TYPE_MCAST_ALL   1
#define EF_FILTER_SPEC_TYPE_MAC_VLAN    2
#define EF_FILTER_SPEC_TYPE_IP4_MCAST   3

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

#define EF_EVENT_TYPE_RX               0
#define EF_EVENT_TYPE_TX               1
#define EF_EVENT_TYPE_RX_REF           2
#define EF_EVENT_TYPE_RX_NO_DESC_TRUNC 3
#define EF_EVENT_TYPE_RX_DISCARD       4

typedef union {
    struct { int type; unsigned data[7]; } generic;
    struct { int type; int desc_id; unsigned len; unsigned flags; } rx;
    struct { int type; int desc_id; } tx;
    struct { int type; int pkt_id; unsigned len; uint64_t timestamp_ns; } rx_ref;
} ef_event;

#define EF_EVENT_TYPE(ev)              ((ev).generic.type)
#define EF_EVENT_RX_BYTES(ev)          ((ev).rx.len)
#define EF_EVENT_RX_DESC_ID(ev)        ((ev).rx.desc_id)
#define EF_EVENT_TX_DESC_ID(ev)        ((ev).tx.desc_id)
#define EF_EVENT_RX_REF_PKT_ID(ev)     ((ev).rx_ref.pkt_id)
#define EF_EVENT_RX_REF_BYTES(ev)      ((ev).rx_ref.len)
#define EF_EVENT_RX_REF_TIMESTAMP(ev)  ((ev).rx_ref.timestamp_ns)

#define EF_VI_EVENT_POLL_MIN_EVS 1

// ---------------------------------------------------------------------------
// Timestamp
// ---------------------------------------------------------------------------

typedef struct { uint64_t tv_sec; uint32_t tv_nsec; } ef_timespec;

#define EF_VI_SYNC_FLAG_CLOCK_IN_SYNC (1u << 0)

// ---------------------------------------------------------------------------
// IP protocols (mock shim — real code uses <netinet/in.h>)
// ---------------------------------------------------------------------------

#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#define IPPROTO_TCP 6
#endif

// ---------------------------------------------------------------------------
// Mock control interface
// Used by tests to inject failures and events.
// ---------------------------------------------------------------------------

namespace efvi_mock {

// Failure injection
extern bool fail_driver_open;
extern bool fail_pd_alloc;
extern bool fail_vi_alloc;
extern bool fail_memreg_alloc;
extern bool fail_pio_alloc;
extern bool fail_filter_add;
extern bool fail_vi_set_alloc;

// NIC arch returned by ef_vi_alloc_from_pd (default EF10)
extern int nic_arch;

// Events queued to be returned by ef_eventq_poll
extern std::vector<ef_event> pending_events;

// Data blobs accessible via efct_vi_rxpkt_get, keyed by pkt_id
struct RxRefData { const void* ptr; size_t len; uint64_t ts_ns; };
extern std::vector<std::pair<int,RxRefData>> rx_ref_store;

// Reset all state to defaults
void reset();

// Helpers to inject events
void push_rx_event(int buf_id, int len);
void push_tx_event(int buf_id);
void push_rx_ref_event(int pkt_id, const void* data, size_t len, uint64_t ts_ns = 0);

} // namespace efvi_mock

// ---------------------------------------------------------------------------
// Function declarations (implemented in mock_efvi.cpp)
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

int  ef_driver_open(ef_driver_handle* dh_out);
void ef_driver_close(ef_driver_handle dh);

int  ef_pd_alloc(ef_pd* pd, ef_driver_handle dh, int ifindex, unsigned flags);
void ef_pd_free(ef_pd* pd, ef_driver_handle dh);

int  ef_vi_alloc_from_pd(ef_vi* vi, ef_driver_handle vi_dh,
                          ef_pd* pd, ef_driver_handle pd_dh,
                          int evq_capacity, int rxq_capacity, int txq_capacity,
                          ef_vi* evq_vi, int evq_flags, ef_vi_flags vi_flags);
void ef_vi_free(ef_vi* vi, ef_driver_handle dh);

const ef_nic_type* ef_vi_nic_type(const ef_vi* vi);

int  ef_memreg_alloc(ef_memreg* mr, ef_driver_handle mr_dh,
                     ef_pd* pd, ef_driver_handle pd_dh,
                     void* base, size_t size);
void ef_memreg_free(ef_memreg* mr, ef_driver_handle dh);

ef_addr ef_memreg_dma_addr(const ef_memreg* mr, size_t offset);

int  ef_pio_alloc(ef_pio* pio, ef_driver_handle dh, ef_pd* pd,
                  ef_driver_handle pd_dh, int pio_size);
void ef_pio_free(ef_pio* pio, ef_driver_handle dh);
int  ef_pio_link_vi(ef_pio* pio, ef_driver_handle pio_dh,
                    ef_vi* vi, ef_driver_handle vi_dh);

int  ef_vi_receive_post(ef_vi* vi, ef_addr addr, int id);
int  ef_eventq_poll(ef_vi* vi, ef_event* evs, int evs_len);
int  ef_vi_receive_get_timestamp_sync(const ef_vi* vi, const void* pkt,
                                      ef_timespec* ts_out, unsigned* flags_out);

void ef_filter_spec_init(ef_filter_spec* fs, ef_filter_flags flags);
int  ef_filter_spec_set_ip4_local(ef_filter_spec* fs, int proto,
                                   unsigned ip, int port);
int  ef_filter_spec_set_multicast_all(ef_filter_spec* fs);
int  ef_filter_spec_set_multicast_mismatch(ef_filter_spec* fs);
int  ef_filter_spec_set_eth_local(ef_filter_spec* fs, int vlan_id,
                                   const uint8_t* mac);

int  ef_vi_filter_add(ef_vi* vi, ef_driver_handle dh,
                      const ef_filter_spec* fs, ef_filter_cookie* cookie_out);
int  ef_vi_filter_del(ef_vi* vi, ef_driver_handle dh,
                      ef_filter_cookie* cookie);

int  ef_vi_transmit_ctpio_copy(ef_vi* vi, const void* buf, size_t len,
                                int threshold);
int  ef_vi_transmit(ef_vi* vi, ef_addr dma_addr, int bytes, int dma_id);
int  ef_vi_transmit_pio(ef_vi* vi, int offset, int bytes, int dma_id);
void ef_vi_transmit_push(ef_vi* vi);

int  ef_vi_stats_query(const ef_vi* vi, ef_driver_handle dh,
                       void* data, int data_size);

// EfCT-specific
int  efct_vi_rxpkt_get(ef_vi* vi, int pkt_id, const void** base_out);
void efct_vi_rxpkt_release(ef_vi* vi, int pkt_id);

// ViSet
int  ef_vi_set_alloc_from_pd(ef_vi_set* vis, ef_driver_handle dh,
                              ef_pd* pd, ef_driver_handle pd_dh,
                              int ifindex, int n_vis,
                              ef_filter_cookie* cookie_out);
void ef_vi_set_free(ef_vi_set* vis, ef_driver_handle dh);

int  ef_vi_alloc_from_set(ef_vi* vi, ef_driver_handle vi_dh,
                           ef_vi_set* vis, ef_driver_handle vis_dh,
                           int index, int evq_cap, int rxq_cap, int txq_cap,
                           ef_vi* evq_vi, int evq_flags, ef_vi_flags vi_flags);

#ifdef __cplusplus
} // extern "C"
#endif

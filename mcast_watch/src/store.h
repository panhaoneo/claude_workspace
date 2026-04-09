/* store.h – Shared data structures for mcast_watch v1.1 */
#pragma once

#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>

#define MAX_CPUS         512
#define MAX_IRQ_QUEUES    64
#define MAX_SOCKETS       64
#define MAX_DROP_REASONS  32
#define MAX_ALERTS        32
#define IF_NAME_LEN       16
#define PROC_NAME_LEN     32
#define ALERT_MSG_LEN    128

/* ── A: NIC & Driver ── */
typedef struct {
    char     ifname[IF_NAME_LEN];
    /* /proc/net/dev */
    uint64_t rx_bytes,   tx_bytes;
    uint64_t rx_packets, tx_packets;
    uint64_t rx_drop,    tx_drop;
    uint64_t rx_errors;
    /* ethtool -S (5s) */
    uint64_t rx_missed_errors;
    uint64_t rx_fifo_errors;
    uint64_t rx_no_buffer_count;
    /* delta (/s) */
    uint64_t rx_pps,     tx_pps;
    uint64_t rx_bps,     tx_bps;
    uint64_t rx_drop_s;
    uint64_t rx_missed_s;
    uint64_t rx_fifo_s;
    uint64_t rx_nobuf_s;
    /* ethtool -g (5s) */
    uint32_t ring_cur;
    uint32_t ring_max;
    /* /proc/net/igmp */
    uint32_t mcast_groups;
} nic_stat_t;

/* ── B: Kernel / softnet ── */
typedef struct {
    uint64_t sn_processed, sn_dropped, sn_squeezed;
    uint64_t sn_processed_s, sn_dropped_s, sn_squeezed_s;
    uint64_t udp_in, udp_rcvbuf_err, udp_in_err;
    uint64_t udp_in_s, udp_rcvbuf_err_s, udp_in_err_s;
    uint32_t rmem_max;
    uint32_t netdev_max_backlog;
} kernel_stat_t;

/* ── C: CPU & IRQ ── */
typedef struct {
    uint64_t user, system, idle, softirq_ticks;
    double   usage_pct;
    double   softirq_pct;
    uint64_t net_rx_sirq;
    uint64_t net_rx_sirq_s;
} cpu_stat_t;

typedef struct {
    char     queue_name[32];
    uint64_t cpu_counts[MAX_CPUS];
    uint64_t total;
} irq_queue_t;

typedef struct {
    int         irqbalance_running;
    irq_queue_t queues[MAX_IRQ_QUEUES];
    int         queue_count;
    int         cpu_count;   /* copy from store for per-queue display */
} irq_stat_t;

/* ── D: per-socket (ss) ── */
typedef struct {
    char     local_addr[48];
    char     proc_name[PROC_NAME_LEN];
    pid_t    pid;
    uint32_t rcvbuf;
    uint64_t drops;
    uint64_t drops_s;
} socket_stat_t;

/* ── E: eBPF drop reason ── */
typedef struct {
    uint32_t reason;
    char     reason_str[48];
    uint64_t count;
    uint64_t count_s;
} drop_reason_stat_t;

typedef struct {
    int                enabled;
    drop_reason_stat_t reasons[MAX_DROP_REASONS];
    int                reason_count;
} ebpf_stat_t;

/* ── F: Diagnosis ── */
typedef enum {
    DIAG_OK   = 0,
    DIAG_WARN = 1,
    DIAG_CRIT = 2
} diag_level_t;

typedef struct {
    diag_level_t level;
    char         layer[24];
    char         summary[80];
    char         cmd[128];
} diag_item_t;

#define MAX_DIAG_ITEMS 12

/* ── Global Store ── */
typedef struct {
    char            ifname[IF_NAME_LEN];

    nic_stat_t      nic;
    kernel_stat_t   kernel;
    cpu_stat_t      cpus[MAX_CPUS];
    int             cpu_count;
    irq_stat_t      irq;
    socket_stat_t   sockets[MAX_SOCKETS];
    int             socket_count;
    ebpf_stat_t     ebpf;
    diag_item_t     diag[MAX_DIAG_ITEMS];
    int             diag_count;

    pthread_mutex_t lock;
    uint64_t        tick;
} mwatch_store_t;

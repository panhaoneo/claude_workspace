#ifndef STORE_H
#define STORE_H

#include <stdint.h>
#include <pthread.h>
#include <net/if.h>
#include <sys/types.h>

#define MAX_NICS    64
#define MAX_CPUS    512
#define MAX_IRQS    256
#define MAX_FLOWS   256
#define MAX_ALERTS  5

typedef struct {
    char     name[IF_NAMESIZE];
    uint64_t rx_bytes, tx_bytes;
    uint64_t rx_packets, tx_packets;
    uint64_t rx_drop, tx_drop;
    uint64_t rx_errors, tx_errors;
    /* per-second deltas */
    uint64_t rx_pps, tx_pps;
    uint64_t rx_bps, tx_bps;
    uint64_t drop_delta;
    int      drop_streak;   /* consecutive ticks with drops */
} nic_stat_t;

typedef struct {
    uint64_t user, nice, system, idle;
    uint64_t iowait, irq, softirq, steal;
    double   usage_pct;
    double   softirq_pct;
} cpu_stat_t;

typedef struct {
    char     name[64];              /* e.g. "eth1-TxRx-0" */
    int      irq_num;
    uint64_t counts[MAX_CPUS];     /* current raw counts */
    uint64_t delta[MAX_CPUS];      /* delta since last tick */
} irq_stat_t;

typedef struct {
    uint64_t processed;
    uint64_t dropped;
    uint64_t time_squeezed;
    uint64_t squeezed_delta;
} softnet_stat_t;

typedef struct {
    uint32_t tcp_estab;
    uint32_t tcp_time_wait;
    uint32_t tcp_close_wait;
    uint32_t udp_inuse;
    uint64_t tcp_retrans;
    uint64_t retrans_delta;
} sock_stat_t;

typedef struct {
    pid_t    pid;
    char     name[64];
    uint64_t rx_bytes, tx_bytes;
    uint64_t rx_bps, tx_bps;
} flow_stat_t;

typedef struct {
    char time_str[16];
    char msg[256];
} alert_t;

typedef struct {
    nic_stat_t      nics[MAX_NICS];
    int             nic_count;
    int             selected_nic;   /* index into nics[], -1 = none */

    cpu_stat_t      cpus[MAX_CPUS];
    int             cpu_count;

    irq_stat_t      irqs[MAX_IRQS];
    int             irq_count;

    softnet_stat_t  softnet[MAX_CPUS];

    sock_stat_t     sock;

    flow_stat_t     flows[MAX_FLOWS];
    int             flow_count;

    alert_t         alerts[MAX_ALERTS];
    int             alert_count;

    pthread_mutex_t lock;
    uint64_t        tick;
} nettop_store_t;

extern nettop_store_t g_store;

void store_init(void);
void store_destroy(void);

#endif /* STORE_H */

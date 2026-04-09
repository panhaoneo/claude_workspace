/* collector.c – Main 1-second /proc data collection thread */
#include "collector.h"
#include "collector_ethtool.h"
#include "collector_ss.h"
#include "diagnose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

/* ── /proc/net/dev ── */
static int read_net_dev(const char *ifname,
                        uint64_t *rx_bytes, uint64_t *rx_packets,
                        uint64_t *rx_errs,  uint64_t *rx_drop,
                        uint64_t *tx_bytes, uint64_t *tx_packets,
                        uint64_t *tx_drop)
{
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return -1;

    char line[512];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }  /* hdr1 */
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }  /* hdr2 */

    while (fgets(line, sizeof(line), f)) {
        char *col = strchr(line, ':');
        if (!col) continue;

        /* extract interface name */
        char iface[IF_NAME_LEN];
        const char *p = line;
        while (*p == ' ') p++;
        size_t len = (size_t)(col - p);
        if (len >= IF_NAME_LEN) len = IF_NAME_LEN - 1;
        strncpy(iface, p, len);
        iface[len] = '\0';
        /* trim trailing spaces */
        while (len > 0 && iface[len-1] == ' ') iface[--len] = '\0';

        if (strcmp(iface, ifname) != 0) continue;

        unsigned long long rb, rp, re, rd, rf, rr, rc, rm;
        unsigned long long tb, tp, te, td, tf, tc, tca, tcp;
        if (sscanf(col + 1,
            "%llu %llu %llu %llu %llu %llu %llu %llu "
            "%llu %llu %llu %llu %llu %llu %llu %llu",
            &rb, &rp, &re, &rd, &rf, &rr, &rc, &rm,
            &tb, &tp, &te, &td, &tf, &tc, &tca, &tcp) == 16) {
            *rx_bytes   = (uint64_t)rb; *rx_packets = (uint64_t)rp;
            *rx_errs    = (uint64_t)re; *rx_drop    = (uint64_t)rd;
            *tx_bytes   = (uint64_t)tb; *tx_packets = (uint64_t)tp;
            *tx_drop    = (uint64_t)td;
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return -1;
}

/* ── /proc/net/softnet_stat (hex fields, sum all CPUs) ── */
static int read_softnet_stat(uint64_t *total, uint64_t *dropped, uint64_t *squeezed) {
    FILE *f = fopen("/proc/net/softnet_stat", "r");
    if (!f) return -1;

    *total = *dropped = *squeezed = 0;
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        unsigned long t = 0, d = 0, s = 0;
        if (sscanf(line, "%lx %lx %lx", &t, &d, &s) >= 3) {
            *total    += (uint64_t)t;
            *dropped  += (uint64_t)d;
            *squeezed += (uint64_t)s;
        }
    }
    fclose(f);
    return 0;
}

/* ── /proc/net/snmp (UDP line) ── */
static int read_udp_snmp(uint64_t *in_dgrams, uint64_t *rcvbuf_err, uint64_t *in_err) {
    FILE *f = fopen("/proc/net/snmp", "r");
    if (!f) return -1;

    *in_dgrams = *rcvbuf_err = *in_err = 0;
    char line[512];
    int  found_hdr = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Udp: ", 5) != 0) continue;
        if (!found_hdr) { found_hdr = 1; continue; }   /* skip header line */
        unsigned long long ind, nop, ine, outd, rcvb, sndb;
        if (sscanf(line + 5, "%llu %llu %llu %llu %llu %llu",
                   &ind, &nop, &ine, &outd, &rcvb, &sndb) >= 5) {
            *in_dgrams  = (uint64_t)ind;
            *in_err     = (uint64_t)ine;
            *rcvbuf_err = (uint64_t)rcvb;
        }
        break;
    }
    fclose(f);
    return 0;
}

/* ── /proc/stat (per-CPU) ── */
/* Keep previous values in static arrays (one collector thread) */
static uint64_t s_prev_total  [MAX_CPUS];
static uint64_t s_prev_idle   [MAX_CPUS];
static uint64_t s_prev_softirq[MAX_CPUS];
static int      s_cpu_init = 0;

static int read_cpu_stat(mwatch_store_t *store) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    char line[512];
    int  max_id = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu", 3) != 0) continue;
        if (line[3] == ' ') continue;  /* aggregate line */

        int id;
        if (sscanf(line + 3, "%d", &id) != 1) continue;
        if (id < 0 || id >= MAX_CPUS) continue;

        unsigned long long user, nice, sys, idle, iow, irq, sirq, steal;
        if (sscanf(line, "cpu%*d %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &sys, &idle, &iow, &irq, &sirq, &steal) < 7)
            continue;

        uint64_t total   = (uint64_t)(user + nice + sys + idle + iow + irq + sirq + steal);
        uint64_t dtotal  = total - s_prev_total[id];
        uint64_t didle   = (uint64_t)idle  - s_prev_idle[id];
        uint64_t dsirq   = (uint64_t)sirq  - s_prev_softirq[id];

        cpu_stat_t *c = &store->cpus[id];
        if (s_cpu_init && dtotal > 0) {
            c->usage_pct    = 100.0 * (double)(dtotal - didle) / (double)dtotal;
            c->softirq_pct  = 100.0 * (double)dsirq            / (double)dtotal;
        }
        c->user          = (uint64_t)user;
        c->system        = (uint64_t)sys;
        c->idle          = (uint64_t)idle;
        c->softirq_ticks = (uint64_t)sirq;

        s_prev_total  [id] = total;
        s_prev_idle   [id] = (uint64_t)idle;
        s_prev_softirq[id] = (uint64_t)sirq;

        if (id + 1 > max_id) max_id = id + 1;
    }
    fclose(f);

    if (!s_cpu_init) s_cpu_init = 1;
    return max_id;
}

/* ── /proc/softirqs (NET_RX per CPU) ── */
static uint64_t s_prev_net_rx[MAX_CPUS];

static void read_softirqs(mwatch_store_t *store) {
    FILE *f = fopen("/proc/softirqs", "r");
    if (!f) return;

    char line[1024];
    /* skip header (CPU column names) */
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; }

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "NET_RX:", 7) != 0 &&
            strncmp(line, " NET_RX:", 8) != 0) continue;

        /* skip label and optional spaces */
        char *p = line;
        while (*p && *p != ':') p++;
        if (*p == ':') p++;

        for (int i = 0; i < store->cpu_count && i < MAX_CPUS; i++) {
            unsigned long long v = 0;
            while (*p == ' ') p++;
            if (sscanf(p, "%llu", &v) != 1) break;
            while (*p && *p != ' ') p++;

            uint64_t uv = (uint64_t)v;
            uint64_t ds = uv - s_prev_net_rx[i];
            store->cpus[i].net_rx_sirq   = uv;
            store->cpus[i].net_rx_sirq_s = ds;
            s_prev_net_rx[i] = uv;
        }
        break;
    }
    fclose(f);
}

/* ── /proc/interrupts (per-interface queue) ── */
static void read_interrupts(mwatch_store_t *store) {
    FILE *f = fopen("/proc/interrupts", "r");
    if (!f) return;

    char line[2048];
    int  q_count = 0;

    /* header: determine CPU count if not known */
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; }

    while (fgets(line, sizeof(line), f) && q_count < MAX_IRQ_QUEUES) {
        /* check if line mentions our interface */
        if (!strstr(line, store->ifname)) continue;

        irq_queue_t *qu = &store->irq.queues[q_count];
        memset(qu, 0, sizeof(*qu));

        /* extract IRQ label (after the counts) – last word on line */
        char *label = NULL;
        char *tok = strtok(line, " \t\n");
        char  col_strs[MAX_CPUS][24];
        int   ncols = 0;

        while (tok) {
            if (ncols < MAX_CPUS)
                snprintf(col_strs[ncols++], 24, "%s", tok);
            label = tok;
            tok = strtok(NULL, " \t\n");
        }

        if (label) snprintf(qu->queue_name, sizeof(qu->queue_name), "%s", label);

        /* columns: [0]=irq_num, [1..ncpus]=counts, [ncpus+1]=type, [ncpus+2]=label */
        int num_cpus = store->irq.cpu_count > 0 ? store->irq.cpu_count : 1;
        uint64_t total = 0;
        for (int i = 1; i <= num_cpus && i < ncols - 2; i++) {
            uint64_t v = strtoull(col_strs[i], NULL, 10);
            qu->cpu_counts[i-1] = v;
            total += v;
        }
        qu->total = total;
        q_count++;
    }
    store->irq.queue_count = q_count;
    fclose(f);
}

/* ── /proc/net/igmp (multicast group count) ── */
static void read_igmp(mwatch_store_t *store) {
    FILE *f = fopen("/proc/net/igmp", "r");
    if (!f) {
        /* try igmp6 as well */
        return;
    }
    char line[256];
    int  count = 0;
    /* skip header */
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; }
    while (fgets(line, sizeof(line), f)) {
        /* lines with a device prefix are per-if entries;
         * lines with leading spaces are group entries */
        if (line[0] == ' ' || line[0] == '\t') {
            char buf[256];
            strncpy(buf, line, sizeof(buf)-1);
            buf[sizeof(buf)-1] = '\0';
            /* find device name context before this line – simplified:
             * just count non-header non-blank lines that contain a device name
             * For simplicity, count all group entries */
            count++;
        }
    }
    store->nic.mcast_groups = (uint32_t)count;
    fclose(f);
}

/* ── sysctl params (5s cadence) ── */
static void read_sysctl(mwatch_store_t *store) {
    FILE *f;
    char buf[64];

    f = fopen("/proc/sys/net/core/rmem_max", "r");
    if (f) {
        if (fgets(buf, sizeof(buf), f))
            store->kernel.rmem_max = (uint32_t)strtoul(buf, NULL, 10);
        fclose(f);
    }
    f = fopen("/proc/sys/net/core/netdev_max_backlog", "r");
    if (f) {
        if (fgets(buf, sizeof(buf), f))
            store->kernel.netdev_max_backlog = (uint32_t)strtoul(buf, NULL, 10);
        fclose(f);
    }
}

/* ── irqbalance detection (5s cadence) ── */
static void check_irqbalance(mwatch_store_t *store) {
    FILE *f = popen("pgrep -x irqbalance 2>/dev/null", "r");
    if (!f) { store->irq.irqbalance_running = 0; return; }
    char buf[16];
    store->irq.irqbalance_running = (fgets(buf, sizeof(buf), f) != NULL) ? 1 : 0;
    pclose(f);
}

/* ── Previous values for /proc delta calculations ── */
static uint64_t prev_rx_bytes, prev_rx_pkts, prev_rx_drop;
static uint64_t prev_tx_bytes, prev_tx_pkts;
static uint64_t prev_sn_total, prev_sn_drop, prev_sn_squeeze;
static uint64_t prev_udp_in, prev_udp_rcvb, prev_udp_inerr;
static uint64_t prev_sock_drops[MAX_SOCKETS];
static int      prev_init = 0;

/* ── Main collector loop ── */
void *collector_thread(void *arg) {
    collector_args_t *ca    = (collector_args_t *)arg;
    mwatch_store_t   *store = ca->store;

    int slow_tick = 0;   /* counts 1-s ticks; every 5 ticks do slow collection */

    struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };

    while (*ca->running) {
        nanosleep(&ts, NULL);

        /* ── Collect raw values (no lock needed for local vars) ── */

        /* /proc/net/dev */
        uint64_t rx_b = 0, rx_p = 0, rx_e = 0, rx_d = 0;
        uint64_t tx_b = 0, tx_p = 0, tx_d = 0;
        read_net_dev(store->ifname, &rx_b, &rx_p, &rx_e, &rx_d,
                     &tx_b, &tx_p, &tx_d);

        /* /proc/net/softnet_stat */
        uint64_t sn_t = 0, sn_d = 0, sn_s = 0;
        read_softnet_stat(&sn_t, &sn_d, &sn_s);

        /* /proc/net/snmp */
        uint64_t udp_in = 0, udp_rcvb = 0, udp_ine = 0;
        read_udp_snmp(&udp_in, &udp_rcvb, &udp_ine);

        /* ss per-socket (1s) */
        socket_stat_t socks[MAX_SOCKETS];
        int sock_count = collect_ss(socks, MAX_SOCKETS);

        /* ── Slow (5-s) collections ── */
        if (slow_tick == 0) {
            collect_ethtool(store);   /* acquires/releases lock itself */
            /* sysctl + irqbalance under lock below */
        }

        /* ── Lock, compute deltas, update store ── */
        pthread_mutex_lock(&store->lock);

        /* CPU stats (need cpu_count first) */
        int cpu_count = read_cpu_stat(store);
        if (cpu_count > 0) store->cpu_count = cpu_count;
        store->irq.cpu_count = store->cpu_count;

        read_softirqs(store);
        read_interrupts(store);
        read_igmp(store);

        if (slow_tick == 0) {
            read_sysctl(store);
            check_irqbalance(store);
        }

        /* NIC deltas */
        if (prev_init) {
            store->nic.rx_pps    = rx_p   - prev_rx_pkts;
            store->nic.tx_pps    = tx_p   - prev_tx_pkts;
            store->nic.rx_bps    = (rx_b  - prev_rx_bytes) * 8;
            store->nic.tx_bps    = (tx_b  - prev_tx_bytes) * 8;
            store->nic.rx_drop_s = rx_d   - prev_rx_drop;
        }
        store->nic.rx_bytes   = rx_b; store->nic.tx_bytes   = tx_b;
        store->nic.rx_packets = rx_p; store->nic.tx_packets = tx_p;
        store->nic.rx_drop    = rx_d; store->nic.tx_drop    = tx_d;
        store->nic.rx_errors  = rx_e;
        prev_rx_bytes = rx_b; prev_rx_pkts = rx_p; prev_rx_drop = rx_d;
        prev_tx_bytes = tx_b; prev_tx_pkts = tx_p;

        /* softnet deltas */
        if (prev_init) {
            store->kernel.sn_processed_s  = sn_t - prev_sn_total;
            store->kernel.sn_dropped_s    = sn_d - prev_sn_drop;
            store->kernel.sn_squeezed_s   = sn_s - prev_sn_squeeze;
        }
        store->kernel.sn_processed = sn_t;
        store->kernel.sn_dropped   = sn_d;
        store->kernel.sn_squeezed  = sn_s;
        prev_sn_total   = sn_t;
        prev_sn_drop    = sn_d;
        prev_sn_squeeze = sn_s;

        /* UDP snmp deltas */
        if (prev_init) {
            store->kernel.udp_in_s         = udp_in   - prev_udp_in;
            store->kernel.udp_rcvbuf_err_s = udp_rcvb - prev_udp_rcvb;
            store->kernel.udp_in_err_s     = udp_ine  - prev_udp_inerr;
        }
        store->kernel.udp_in         = udp_in;
        store->kernel.udp_rcvbuf_err = udp_rcvb;
        store->kernel.udp_in_err     = udp_ine;
        prev_udp_in    = udp_in;
        prev_udp_rcvb  = udp_rcvb;
        prev_udp_inerr = udp_ine;

        /* per-socket drops delta */
        for (int i = 0; i < sock_count; i++) {
            socks[i].drops_s = (prev_init && i < MAX_SOCKETS)
                               ? socks[i].drops - prev_sock_drops[i] : 0;
        }
        for (int i = 0; i < sock_count && i < MAX_SOCKETS; i++)
            prev_sock_drops[i] = socks[i].drops;

        store->socket_count = sock_count;
        for (int i = 0; i < sock_count; i++)
            store->sockets[i] = socks[i];

        if (!prev_init) prev_init = 1;

        /* count_s for eBPF reasons is maintained by ebpf_thread */

        diagnose(store);
        store->tick++;

        pthread_mutex_unlock(&store->lock);

        slow_tick = (slow_tick + 1) % 5;
    }
    return NULL;
}

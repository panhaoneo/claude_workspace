#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>
#include "collector.h"
#include "store.h"
#include "alert.h"

/* ------------------------------------------------------------------ */
/* Internal previous-tick snapshots (not shared, no lock needed)       */
/* ------------------------------------------------------------------ */
static nic_stat_t     prev_nics[MAX_NICS];
static int            prev_nic_count;
static cpu_stat_t     prev_cpus[MAX_CPUS];
static softnet_stat_t prev_softnet[MAX_CPUS];
static uint64_t       prev_irq[MAX_IRQS][MAX_CPUS];

/* Flow previous snapshot */
typedef struct {
    pid_t    pid;
    uint64_t rx_bytes, tx_bytes;
} flow_prev_t;
static flow_prev_t prev_flows[MAX_FLOWS];
static int         prev_flow_count;

static volatile int running = 1;

void collector_stop(void)
{
    running = 0;
}

/* ------------------------------------------------------------------ */
/* Helper: nanosleep 1000ms                                            */
/* ------------------------------------------------------------------ */
static void sleep_1s(void)
{
    struct timespec ts = {1, 0};
    nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------------ */
/* collect_nic: /proc/net/dev                                          */
/* ------------------------------------------------------------------ */
static void collect_nic(void)
{
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) return;

    /* Snapshot arrays (local) */
    nic_stat_t cur[MAX_NICS];
    int        cnt = 0;
    char       line[512];

    /* Skip 2 header lines */
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return; }
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return; }

    while (fgets(line, sizeof(line), fp) && cnt < MAX_NICS) {
        nic_stat_t *n = &cur[cnt];
        memset(n, 0, sizeof(*n));

        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        /* trim leading spaces from name */
        char *p = line;
        while (*p == ' ') p++;
        snprintf(n->name, sizeof(n->name), "%.*s",
                 (int)(sizeof(n->name) - 1), p);

        /* Parse: rx_bytes packets errs drop fifo frame compressed mcast
                  tx_bytes packets errs drop fifo colls carrier compressed */
        uint64_t rx_fifo, rx_frame, rx_comp, rx_mcast;
        uint64_t tx_fifo, tx_colls, tx_carrier, tx_comp;
        sscanf(colon + 1,
               "%lu %lu %lu %lu %lu %lu %lu %lu "
               "%lu %lu %lu %lu %lu %lu %lu %lu",
               &n->rx_bytes, &n->rx_packets, &n->rx_errors, &n->rx_drop,
               &rx_fifo, &rx_frame, &rx_comp, &rx_mcast,
               &n->tx_bytes, &n->tx_packets, &n->tx_errors, &n->tx_drop,
               &tx_fifo, &tx_colls, &tx_carrier, &tx_comp);
        cnt++;
    }
    fclose(fp);

    pthread_mutex_lock(&g_store.lock);

    /* Carry over drop_streak from previous store data */
    for (int i = 0; i < cnt; i++) {
        for (int j = 0; j < g_store.nic_count; j++) {
            if (strcmp(cur[i].name, g_store.nics[j].name) == 0) {
                cur[i].drop_streak = g_store.nics[j].drop_streak;
                break;
            }
        }
    }

    for (int i = 0; i < cnt; i++) {
        nic_stat_t *n = &g_store.nics[i];
        /* Find matching previous snapshot */
        int found = 0;
        for (int j = 0; j < prev_nic_count; j++) {
            if (strcmp(cur[i].name, prev_nics[j].name) == 0) {
                uint64_t drx_b = cur[i].rx_bytes   - prev_nics[j].rx_bytes;
                uint64_t dtx_b = cur[i].tx_bytes   - prev_nics[j].tx_bytes;
                uint64_t drx_p = cur[i].rx_packets - prev_nics[j].rx_packets;
                uint64_t dtx_p = cur[i].tx_packets - prev_nics[j].tx_packets;
                uint64_t ddrop = (cur[i].rx_drop + cur[i].tx_drop) -
                                 (prev_nics[j].rx_drop + prev_nics[j].tx_drop);
                cur[i].rx_bps      = drx_b;   /* bytes/s (interval = 1s) */
                cur[i].tx_bps      = dtx_b;
                cur[i].rx_pps      = drx_p;
                cur[i].tx_pps      = dtx_p;
                cur[i].drop_delta  = ddrop;
                found = 1;
                break;
            }
        }
        (void)found;
        *n = cur[i];
    }
    g_store.nic_count = cnt;

    pthread_mutex_unlock(&g_store.lock);

    /* Save snapshot for next tick */
    memcpy(prev_nics, cur, sizeof(nic_stat_t) * cnt);
    prev_nic_count = cnt;
}

/* ------------------------------------------------------------------ */
/* collect_cpu: /proc/stat                                             */
/* ------------------------------------------------------------------ */
static void collect_cpu(void)
{
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return;

    cpu_stat_t cur[MAX_CPUS];
    int        cnt = 0;
    char       line[256];

    while (fgets(line, sizeof(line), fp) && cnt < MAX_CPUS) {
        if (strncmp(line, "cpu", 3) != 0) break;
        if (line[3] == ' ') continue; /* skip aggregate "cpu " line */

        cpu_stat_t *c = &cur[cnt];
        memset(c, 0, sizeof(*c));
        int cpu_id;
        sscanf(line, "cpu%d %lu %lu %lu %lu %lu %lu %lu %lu",
               &cpu_id,
               &c->user, &c->nice, &c->system, &c->idle,
               &c->iowait, &c->irq, &c->softirq, &c->steal);
        cnt++;
    }
    fclose(fp);

    pthread_mutex_lock(&g_store.lock);

    for (int i = 0; i < cnt; i++) {
        cpu_stat_t *c = &g_store.cpus[i];
        if (i < cnt && prev_cpus[i].idle != 0) {
            uint64_t prev_total = prev_cpus[i].user + prev_cpus[i].nice +
                                  prev_cpus[i].system + prev_cpus[i].idle +
                                  prev_cpus[i].iowait + prev_cpus[i].irq +
                                  prev_cpus[i].softirq + prev_cpus[i].steal;
            uint64_t cur_total  = cur[i].user + cur[i].nice +
                                  cur[i].system + cur[i].idle +
                                  cur[i].iowait + cur[i].irq +
                                  cur[i].softirq + cur[i].steal;
            uint64_t dtotal = cur_total  - prev_total;
            if (dtotal > 0) {
                uint64_t didle    = cur[i].idle    - prev_cpus[i].idle;
                uint64_t dsoftirq = cur[i].softirq - prev_cpus[i].softirq;
                cur[i].usage_pct   = 100.0 * (dtotal - didle) / dtotal;
                cur[i].softirq_pct = 100.0 * dsoftirq / dtotal;
            }
        }
        *c = cur[i];
    }
    g_store.cpu_count = cnt;

    pthread_mutex_unlock(&g_store.lock);

    memcpy(prev_cpus, cur, sizeof(cpu_stat_t) * cnt);
}

/* ------------------------------------------------------------------ */
/* collect_irq: /proc/interrupts                                       */
/* We only keep IRQs whose name contains a NIC name from the store.   */
/* ------------------------------------------------------------------ */
static void collect_irq(void)
{
    FILE *fp = fopen("/proc/interrupts", "r");
    if (!fp) return;

    irq_stat_t cur[MAX_IRQS];
    int        cnt = 0;
    char       line[4096];
    int        ncpus = 0;

    /* First line: "           CPU0   CPU1 ..." */
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return; }
    {
        char *p = line;
        while ((p = strstr(p, "CPU")) != NULL) { ncpus++; p += 3; }
    }
    if (ncpus == 0) ncpus = 1;
    if (ncpus > MAX_CPUS) ncpus = MAX_CPUS;

    while (fgets(line, sizeof(line), fp) && cnt < MAX_IRQS) {
        /* Skip non-numeric IRQ lines (like "NMI:", "LOC:", etc.)
         * that don't relate to network queues */
        char *colon = strchr(line, ':');
        if (!colon) continue;

        /* IRQ number / name */
        char irq_label[32];
        int  label_len = (int)(colon - line);
        if (label_len <= 0 || label_len >= (int)sizeof(irq_label)) continue;
        memcpy(irq_label, line, label_len);
        irq_label[label_len] = '\0';
        /* trim leading spaces */
        char *lp = irq_label;
        while (*lp == ' ') lp++;

        /* Parse CPU counts */
        uint64_t counts[MAX_CPUS];
        memset(counts, 0, sizeof(uint64_t) * ncpus);
        char *p = colon + 1;
        for (int c = 0; c < ncpus; c++) {
            while (*p == ' ') p++;
            counts[c] = strtoull(p, &p, 10);
        }

        /* The rest of the line is the device/action name */
        while (*p == ' ') p++;
        /* Remove newline */
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';

        /* Check if this IRQ is associated with any known NIC */
        int is_net = 0;
        pthread_mutex_lock(&g_store.lock);
        for (int n = 0; n < g_store.nic_count; n++) {
            if (strstr(p, g_store.nics[n].name) != NULL) {
                is_net = 1;
                break;
            }
        }
        pthread_mutex_unlock(&g_store.lock);
        if (!is_net) continue;

        irq_stat_t *irq = &cur[cnt];
        memset(irq, 0, sizeof(*irq));
        snprintf(irq->name, sizeof(irq->name), "%s", p);
        irq->irq_num = atoi(lp);
        memcpy(irq->counts, counts, sizeof(uint64_t) * ncpus);

        /* Compute delta */
        for (int c = 0; c < ncpus; c++) {
            irq->delta[c] = counts[c] - prev_irq[cnt][c];
        }

        cnt++;
    }
    fclose(fp);

    pthread_mutex_lock(&g_store.lock);
    for (int i = 0; i < cnt; i++) {
        g_store.irqs[i] = cur[i];
    }
    g_store.irq_count = cnt;
    pthread_mutex_unlock(&g_store.lock);

    /* Save for next tick */
    for (int i = 0; i < cnt; i++) {
        memcpy(prev_irq[i], cur[i].counts, sizeof(uint64_t) * ncpus);
    }
}

/* ------------------------------------------------------------------ */
/* collect_softnet: /proc/net/softnet_stat                             */
/* ------------------------------------------------------------------ */
static void collect_softnet(void)
{
    FILE *fp = fopen("/proc/net/softnet_stat", "r");
    if (!fp) return;

    softnet_stat_t cur[MAX_CPUS];
    int            cnt = 0;
    char           line[512];

    while (fgets(line, sizeof(line), fp) && cnt < MAX_CPUS) {
        uint64_t processed, dropped, squeezed;
        /* Fields are space-separated hex values */
        if (sscanf(line, "%lx %lx %lx", &processed, &dropped, &squeezed) != 3)
            break;
        cur[cnt].processed    = processed;
        cur[cnt].dropped      = dropped;
        cur[cnt].time_squeezed = squeezed;
        cur[cnt].squeezed_delta = (squeezed >= prev_softnet[cnt].time_squeezed)
                                  ? squeezed - prev_softnet[cnt].time_squeezed
                                  : 0;
        cnt++;
    }
    fclose(fp);

    pthread_mutex_lock(&g_store.lock);
    for (int i = 0; i < cnt; i++) {
        g_store.softnet[i] = cur[i];
    }
    pthread_mutex_unlock(&g_store.lock);

    memcpy(prev_softnet, cur, sizeof(softnet_stat_t) * cnt);
}

/* ------------------------------------------------------------------ */
/* collect_sockstat: /proc/net/sockstat                                */
/* ------------------------------------------------------------------ */
static void collect_sockstat(void)
{
    FILE *fp = fopen("/proc/net/sockstat", "r");
    if (!fp) return;

    sock_stat_t s;
    memset(&s, 0, sizeof(s));
    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "TCP:", 4) == 0) {
            uint32_t inuse, orphan, tw, alloc;
            int mem;
            sscanf(line, "TCP: inuse %u orphan %u tw %u alloc %u mem %d",
                   &inuse, &orphan, &tw, &alloc, &mem);
            s.tcp_estab     = inuse;
            s.tcp_time_wait = tw;
        } else if (strncmp(line, "UDP:", 4) == 0) {
            uint32_t inuse;
            int mem;
            sscanf(line, "UDP: inuse %u mem %d", &inuse, &mem);
            s.udp_inuse = inuse;
        }
    }
    fclose(fp);

    /* TCP retransmit from /proc/net/snmp */
    FILE *snmp = fopen("/proc/net/snmp", "r");
    if (snmp) {
        char hdr[512], val[512];
        while (fgets(hdr, sizeof(hdr), snmp)) {
            if (strncmp(hdr, "Tcp:", 4) == 0) {
                if (!fgets(val, sizeof(val), snmp)) break;
                /* Find RetransSegs field */
                char *hdrp = hdr + 4;
                char *valp = val + 4;
                char hfield[64], vfield[64];
                while (sscanf(hdrp, "%63s%n", hfield, &(int){0}) == 1 &&
                       sscanf(valp, "%63s%n", vfield, &(int){0}) == 1) {
                    /* advance pointers */
                    while (*hdrp && *hdrp != ' ') hdrp++;
                    while (*hdrp == ' ') hdrp++;
                    while (*valp && *valp != ' ') valp++;
                    while (*valp == ' ') valp++;
                    if (strcmp(hfield, "RetransSegs") == 0) {
                        uint64_t retrans = strtoull(vfield, NULL, 10);
                        pthread_mutex_lock(&g_store.lock);
                        s.retrans_delta = retrans - g_store.sock.tcp_retrans;
                        s.tcp_retrans   = retrans;
                        pthread_mutex_unlock(&g_store.lock);
                        break;
                    }
                }
                break;
            }
        }
        fclose(snmp);
    }

    pthread_mutex_lock(&g_store.lock);
    /* preserve retrans if not updated above */
    if (s.tcp_retrans == 0) {
        s.tcp_retrans   = g_store.sock.tcp_retrans;
        s.retrans_delta = g_store.sock.retrans_delta;
    }
    g_store.sock = s;
    pthread_mutex_unlock(&g_store.lock);
}

/* ------------------------------------------------------------------ */
/* collect_flows: /proc/[pid]/net/dev                                  */
/* ------------------------------------------------------------------ */
static void collect_flows(void)
{
    flow_stat_t cur[MAX_FLOWS];
    int         cnt = 0;

    DIR *proc = opendir("/proc");
    if (!proc) return;

    struct dirent *de;
    while ((de = readdir(proc)) != NULL && cnt < MAX_FLOWS) {
        /* Only numeric directories = PID */
        if (!isdigit((unsigned char)de->d_name[0])) continue;
        pid_t pid = (pid_t)atoi(de->d_name);

        char path[128];
        snprintf(path, sizeof(path), "/proc/%d/net/dev", pid);
        FILE *fp = fopen(path, "r");
        if (!fp) continue;

        char line[512];
        /* Skip 2 header lines */
        if (!fgets(line, sizeof(line), fp)) { fclose(fp); continue; }
        if (!fgets(line, sizeof(line), fp)) { fclose(fp); continue; }

        uint64_t total_rx = 0, total_tx = 0;
        int has_data = 0;
        while (fgets(line, sizeof(line), fp)) {
            char *colon = strchr(line, ':');
            if (!colon) continue;
            uint64_t rx_b, tx_b;
            uint64_t dummy;
            /* skip interface name, read rx_bytes, skip 7, tx_bytes */
            if (sscanf(colon + 1,
                       "%lu %lu %lu %lu %lu %lu %lu %lu "
                       "%lu",
                       &rx_b, &dummy, &dummy, &dummy,
                       &dummy, &dummy, &dummy, &dummy,
                       &tx_b) >= 9) {
                total_rx += rx_b;
                total_tx += tx_b;
                has_data  = 1;
            }
        }
        fclose(fp);

        if (!has_data) continue;

        flow_stat_t *f = &cur[cnt];
        f->pid      = pid;
        f->rx_bytes = total_rx;
        f->tx_bytes = total_tx;
        f->rx_bps   = 0;
        f->tx_bps   = 0;

        /* Process name from /proc/[pid]/comm */
        snprintf(path, sizeof(path), "/proc/%d/comm", pid);
        FILE *comm = fopen(path, "r");
        if (comm) {
            if (fgets(f->name, sizeof(f->name), comm)) {
                char *nl = strchr(f->name, '\n');
                if (nl) *nl = '\0';
            }
            fclose(comm);
        } else {
            snprintf(f->name, sizeof(f->name), "pid%d", pid);
        }

        /* Delta against prev snapshot */
        for (int j = 0; j < prev_flow_count; j++) {
            if (prev_flows[j].pid == pid) {
                f->rx_bps = (total_rx >= prev_flows[j].rx_bytes)
                            ? total_rx - prev_flows[j].rx_bytes : 0;
                f->tx_bps = (total_tx >= prev_flows[j].tx_bytes)
                            ? total_tx - prev_flows[j].tx_bytes : 0;
                break;
            }
        }

        cnt++;
    }
    closedir(proc);

    /* Sort by rx_bps + tx_bps descending (simple bubble for small n) */
    for (int i = 0; i < cnt - 1; i++) {
        for (int j = i + 1; j < cnt; j++) {
            if ((cur[j].rx_bps + cur[j].tx_bps) >
                (cur[i].rx_bps + cur[i].tx_bps)) {
                flow_stat_t tmp = cur[i];
                cur[i] = cur[j];
                cur[j] = tmp;
            }
        }
    }

    pthread_mutex_lock(&g_store.lock);
    memcpy(g_store.flows, cur, sizeof(flow_stat_t) * cnt);
    g_store.flow_count = cnt;
    pthread_mutex_unlock(&g_store.lock);

    /* Save prev snapshot */
    for (int i = 0; i < cnt; i++) {
        prev_flows[i].pid      = cur[i].pid;
        prev_flows[i].rx_bytes = cur[i].rx_bytes;
        prev_flows[i].tx_bytes = cur[i].tx_bytes;
    }
    prev_flow_count = cnt;
}

/* ------------------------------------------------------------------ */
/* Thread entry                                                         */
/* ------------------------------------------------------------------ */
void *collector_thread(void *arg)
{
    (void)arg;

    /* First pass: populate prev snapshots */
    collect_nic();
    collect_cpu();
    collect_softnet();
    sleep_1s();

    while (running) {
        collect_nic();
        collect_cpu();
        collect_irq();
        collect_softnet();
        collect_sockstat();
        collect_flows();

        pthread_mutex_lock(&g_store.lock);
        alert_evaluate();
        g_store.tick++;
        pthread_mutex_unlock(&g_store.lock);

        sleep_1s();
    }
    return NULL;
}

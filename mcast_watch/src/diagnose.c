/* diagnose.c – Diagnostic engine
 * Called under store lock; reads current metrics and writes store->diag[].
 */
#include "diagnose.h"
#include <string.h>
#include <stdio.h>

#define DIAG_APPEND(store, lvl, lyr, sum, cm) do { \
    int _i = (store)->diag_count; \
    if (_i < MAX_DIAG_ITEMS) { \
        (store)->diag[_i].level = (lvl); \
        snprintf((store)->diag[_i].layer,   sizeof((store)->diag[_i].layer),   "%s", (lyr)); \
        snprintf((store)->diag[_i].summary, sizeof((store)->diag[_i].summary), "%s", (sum)); \
        snprintf((store)->diag[_i].cmd,     sizeof((store)->diag[_i].cmd),     "%s", (cm)); \
        (store)->diag_count++; \
    } \
} while (0)

void diagnose(mwatch_store_t *store) {
    store->diag_count = 0;
    const nic_stat_t    *nic    = &store->nic;
    const kernel_stat_t *kern   = &store->kernel;
    const ebpf_stat_t   *ebpf   = &store->ebpf;
    const irq_stat_t    *irq    = &store->irq;
    char sum[80], cmd[128];
    int nic_ok = 1, softnet_ok = 1, udp_ok = 1, cpu_ok = 1;

    /* ── Case 1: NIC hardware / driver drops ── */
    if (nic->rx_missed_s > 0 || nic->rx_nobuf_s > 0) {
        snprintf(sum, sizeof(sum), "rx_missed %llu/s rx_nobuf %llu/s -> HW ring overflow",
                 (unsigned long long)nic->rx_missed_s,
                 (unsigned long long)nic->rx_nobuf_s);
        snprintf(cmd, sizeof(cmd), "ethtool -G %s rx 4096", store->ifname);
        DIAG_APPEND(store, DIAG_CRIT, "NIC DROP", sum, cmd);
        nic_ok = 0;
    } else if (nic->rx_drop_s > 0) {
        snprintf(sum, sizeof(sum), "rx_drop %llu/s -> driver queue drop",
                 (unsigned long long)nic->rx_drop_s);
        snprintf(cmd, sizeof(cmd), "ethtool -G %s rx 4096", store->ifname);
        DIAG_APPEND(store, DIAG_WARN, "DRIVER DROP", sum, cmd);
        nic_ok = 0;
    }

    /* ── Case 2: softirq / backlog drops ── */
    if (kern->sn_dropped_s > 0) {
        snprintf(sum, sizeof(sum), "softnet_dropped %llu/s -> CPU backlog overflow",
                 (unsigned long long)kern->sn_dropped_s);
        snprintf(cmd, sizeof(cmd), "sysctl -w net.core.netdev_max_backlog=200000");
        DIAG_APPEND(store, DIAG_WARN, "SOFTIRQ DROP", sum, cmd);
        softnet_ok = 0;
    }
    if (kern->sn_squeezed_s > 0) {
        snprintf(sum, sizeof(sum), "time_squeezed %llu/s -> softirq budget exhausted",
                 (unsigned long long)kern->sn_squeezed_s);
        snprintf(cmd, sizeof(cmd), "sysctl -w net.core.netdev_budget=600");
        DIAG_APPEND(store, DIAG_WARN, "SOFTIRQ SQUEEZE", sum, cmd);
        softnet_ok = 0;
    }

    /* ── Case 3: UDP buffer drops ── */
    if (kern->udp_rcvbuf_err_s > 0) {
        /* Check eBPF for exact reason */
        const char *reason_str = "";
        if (ebpf->enabled) {
            for (int i = 0; i < ebpf->reason_count; i++) {
                if (ebpf->reasons[i].count_s > 0 &&
                    strstr(ebpf->reasons[i].reason_str, "UDP_SOCK_FULL")) {
                    reason_str = " [eBPF: UDP_SOCK_FULL confirmed]";
                    break;
                }
            }
        }
        snprintf(sum, sizeof(sum), "RcvbufErrors %llu/s buf full%s",
                 (unsigned long long)kern->udp_rcvbuf_err_s, reason_str);
        snprintf(cmd, sizeof(cmd), "sysctl -w net.core.rmem_max=134217728");
        DIAG_APPEND(store, DIAG_WARN, "UDP BUF FULL", sum, cmd);
        udp_ok = 0;
    }

    /* ── Case 4: per-socket drops ── */
    for (int i = 0; i < store->socket_count; i++) {
        const socket_stat_t *s = &store->sockets[i];
        if (s->drops_s > 0) {
            snprintf(sum, sizeof(sum), "%s %s drops %llu/s -> socket buf too small",
                     s->proc_name[0] ? s->proc_name : "?",
                     s->local_addr, (unsigned long long)s->drops_s);
            snprintf(cmd, sizeof(cmd),
                     "setsockopt SO_RCVBUF or sysctl net.core.rmem_max=134217728");
            DIAG_APPEND(store, DIAG_WARN, "SOCKET DROP", sum, cmd);
            udp_ok = 0;
        }
    }

    /* ── Case 5: CPU softirq bottleneck ── */
    for (int i = 0; i < store->cpu_count; i++) {
        if (store->cpus[i].softirq_pct > 30.0) {
            snprintf(sum, sizeof(sum), "CPU%d softirq %.1f%% -> net RX saturated",
                     i, store->cpus[i].softirq_pct);
            snprintf(cmd, sizeof(cmd),
                     "check IRQ affinity, consider RPS/RFS or more RX queues");
            DIAG_APPEND(store, DIAG_WARN, "CPU BOTTLENECK", sum, cmd);
            cpu_ok = 0;
            break;  /* one warning per cycle is enough */
        }
    }

    /* ── Case 6: IRQ imbalance ── */
    for (int q = 0; q < irq->queue_count; q++) {
        const irq_queue_t *qu = &irq->queues[q];
        if (qu->total == 0) continue;
        uint64_t dom_cpu_cnt = 0;
        for (int c = 0; c < irq->cpu_count && c < MAX_CPUS; c++) {
            if (qu->cpu_counts[c] > dom_cpu_cnt) dom_cpu_cnt = qu->cpu_counts[c];
        }
        if (dom_cpu_cnt * 100 / qu->total > 80) {
            snprintf(sum, sizeof(sum), "queue %s >80%% IRQ on one CPU",
                     qu->queue_name);
            snprintf(cmd, sizeof(cmd),
                     "systemctl stop irqbalance && set /proc/irq/N/smp_affinity");
            DIAG_APPEND(store, DIAG_WARN, "IRQ IMBALANCE", sum, cmd);
            break;
        }
    }

    /* ── Case 7: irqbalance running (HFT concern) ── */
    if (irq->irqbalance_running) {
        DIAG_APPEND(store, DIAG_WARN, "IRQBALANCE ON",
                    "irqbalance running - may interfere with IRQ affinity",
                    "systemctl stop irqbalance");
    }

    /* ── Case 8: eBPF-only reason (not already covered by case 3) ── */
    if (ebpf->enabled && udp_ok) {
        for (int i = 0; i < ebpf->reason_count; i++) {
            if (ebpf->reasons[i].count_s > 0) {
                snprintf(sum, sizeof(sum), "kfree_skb %s %llu/s",
                         ebpf->reasons[i].reason_str,
                         (unsigned long long)ebpf->reasons[i].count_s);
                DIAG_APPEND(store, DIAG_WARN, "EBPF DROP", sum, "");
                break;
            }
        }
    }

    /* ── OK confirmations ── */
    if (nic_ok) {
        char ring_info[32] = "";
        if (nic->ring_max > 0) {
            snprintf(ring_info, sizeof(ring_info), ", ring %u%%",
                     nic->ring_cur * 100 / nic->ring_max);
        }
        snprintf(sum, sizeof(sum), "no HW drops%s", ring_info);
        DIAG_APPEND(store, DIAG_OK, "NIC/DRIVER", sum, "");
    }
    if (softnet_ok) {
        DIAG_APPEND(store, DIAG_OK, "SOFTNET", "no backlog drops", "");
    }
    if (udp_ok) {
        DIAG_APPEND(store, DIAG_OK, "UDP LAYER", "no buffer overflow", "");
    }
    if (cpu_ok) {
        DIAG_APPEND(store, DIAG_OK, "CPU/IRQ", "softirq load normal", "");
    }
}

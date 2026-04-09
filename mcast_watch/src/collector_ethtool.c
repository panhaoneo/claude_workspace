/* collector_ethtool.c – ethtool -S / -g data collection */
#include "collector_ethtool.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Fuzzy-match a stat line key against a list of candidate substrings. */
static int key_matches(const char *key, const char * const *candidates) {
    for (int i = 0; candidates[i] != NULL; i++) {
        if (strstr(key, candidates[i]) != NULL)
            return 1;
    }
    return 0;
}

void collect_ethtool(mwatch_store_t *store) {
    char cmd[128];
    char line[256];
    FILE *fp;

    /* ── ethtool -S: driver counters ── */
    snprintf(cmd, sizeof(cmd), "ethtool -S %s 2>/dev/null", store->ifname);
    fp = popen(cmd, "r");
    if (fp) {
        static const char *missed_keys[] = {"rx_missed", "missed_errors",
                                             "rx_no_buffer_count", NULL};
        static const char *fifo_keys[]   = {"rx_fifo", "fifo_errors", NULL};
        static const char *nobuf_keys[]  = {"no_buffer", "rx_nobuf",
                                             "rx_no_buffer", NULL};

        uint64_t missed = 0, fifo = 0, nobuf = 0;

        while (fgets(line, sizeof(line), fp)) {
            char key[128];
            unsigned long long val;
            if (sscanf(line, " %127[^:]: %llu", key, &val) != 2)
                continue;

            if (key_matches(key, missed_keys))      missed += val;
            else if (key_matches(key, fifo_keys))   fifo   += val;
            else if (key_matches(key, nobuf_keys))  nobuf  += val;
        }
        pclose(fp);

        pthread_mutex_lock(&store->lock);
        /* compute /s deltas against previous ethtool snapshot */
        static uint64_t prev_missed = 0, prev_fifo = 0, prev_nobuf = 0;
        store->nic.rx_missed_errors  = missed;
        store->nic.rx_fifo_errors    = fifo;
        store->nic.rx_no_buffer_count= nobuf;
        /* deltas are per-5s; divide by 5 to get per-second */
        store->nic.rx_missed_s = (missed > prev_missed) ? (missed - prev_missed) / 5 : 0;
        store->nic.rx_fifo_s   = (fifo   > prev_fifo)   ? (fifo   - prev_fifo)   / 5 : 0;
        store->nic.rx_nobuf_s  = (nobuf  > prev_nobuf)  ? (nobuf  - prev_nobuf)  / 5 : 0;
        prev_missed = missed; prev_fifo = fifo; prev_nobuf = nobuf;
        pthread_mutex_unlock(&store->lock);
    }

    /* ── ethtool -g: ring buffer sizes ── */
    snprintf(cmd, sizeof(cmd), "ethtool -g %s 2>/dev/null", store->ifname);
    fp = popen(cmd, "r");
    if (fp) {
        uint32_t ring_max = 0, ring_cur = 0;
        int in_current = 0;

        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "Current hardware settings:")) {
                in_current = 1;
                continue;
            }
            if (strstr(line, "Pre-set maximums:")) {
                in_current = 0;
            }

            unsigned int v;
            if (!in_current && strncmp(line, "RX:", 3) == 0) {
                if (sscanf(line + 3, "%u", &v) == 1) ring_max = v;
            }
            if (in_current && strncmp(line, "RX:", 3) == 0) {
                if (sscanf(line + 3, "%u", &v) == 1) ring_cur = v;
            }
        }
        pclose(fp);

        pthread_mutex_lock(&store->lock);
        if (ring_max > 0) {
            store->nic.ring_max = ring_max;
            store->nic.ring_cur = ring_cur;
        }
        pthread_mutex_unlock(&store->lock);
    }
}

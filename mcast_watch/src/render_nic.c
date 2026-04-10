/* render_nic.c – Area A: NIC & Driver rendering */
#include "render_nic.h"
#include <stdio.h>
#include <string.h>

/* Colour pairs defined in ui.c */
#define COL_NORMAL  1
#define COL_WARN    2
#define COL_CRIT    3
#define COL_OK      4
#define COL_HEADER  5

static int drop_color(uint64_t val) {
    if (val == 0) return COL_NORMAL;
    if (val < 100) return COL_WARN;
    return COL_CRIT;
}

static const char *bar_char(uint64_t val) {
    if (val == 0) return " ";   /* none */
    if (val < 100) return "~";  /* warn */
    return "#";                 /* crit */
}

/* Compact bandwidth formatter: "1.23 Gbps" */
static void fmt_bps(char *buf, size_t sz, uint64_t bps) {
    if (bps >= 1000000000ULL)
        snprintf(buf, sz, "%.2f Gbps", (double)bps / 1e9);
    else if (bps >= 1000000ULL)
        snprintf(buf, sz, "%.2f Mbps", (double)bps / 1e6);
    else if (bps >= 1000ULL)
        snprintf(buf, sz, "%.2f Kbps", (double)bps / 1e3);
    else
        snprintf(buf, sz, "%llu bps", (unsigned long long)bps);
}

static void fmt_pps(char *buf, size_t sz, uint64_t pps) {
    if (pps >= 1000000ULL)
        snprintf(buf, sz, "%.2fM pps", (double)pps / 1e6);
    else if (pps >= 1000ULL)
        snprintf(buf, sz, "%.2fK pps", (double)pps / 1e3);
    else
        snprintf(buf, sz, "%llu pps", (unsigned long long)pps);
}

void render_nic(WINDOW *win, const mwatch_store_t *store, int *row, int width) {
    const nic_stat_t *n = &store->nic;
    int r = *row;

    /* Section header */
    wattron(win, COLOR_PAIR(COL_HEADER) | A_BOLD);
    mvwprintw(win, r++, 0, "%-*s", width, "-- NIC & DRIVER -------------------------------------------------------");
    wattroff(win, COLOR_PAIR(COL_HEADER) | A_BOLD);

    /* RX / TX throughput */
    char rx_pps[20], rx_bps[20], tx_pps[20], tx_bps[20];
    fmt_pps(rx_pps, sizeof(rx_pps), n->rx_pps);
    fmt_bps(rx_bps, sizeof(rx_bps), n->rx_bps);
    fmt_pps(tx_pps, sizeof(tx_pps), n->tx_pps);
    fmt_bps(tx_bps, sizeof(tx_bps), n->tx_bps);
    wattron(win, COLOR_PAIR(COL_NORMAL));
    mvwprintw(win, r++, 0, " RX %-12s %-12s  TX %-12s %-12s",
              rx_pps, rx_bps, tx_pps, tx_bps);
    wattroff(win, COLOR_PAIR(COL_NORMAL));

    /* Drop indicators */
    wattron(win, COLOR_PAIR(COL_NORMAL));
    mvwprintw(win, r, 0, " rx_drop ");
    wattroff(win, COLOR_PAIR(COL_NORMAL));
    wattron(win, COLOR_PAIR(drop_color(n->rx_drop_s)));
    wprintw(win, "%s %5llu/s", bar_char(n->rx_drop_s),
            (unsigned long long)n->rx_drop_s);
    wattroff(win, COLOR_PAIR(drop_color(n->rx_drop_s)));

    wattron(win, COLOR_PAIR(COL_NORMAL));
    wprintw(win, "  rx_missed ");
    wattroff(win, COLOR_PAIR(COL_NORMAL));
    wattron(win, COLOR_PAIR(drop_color(n->rx_missed_s)));
    wprintw(win, "%s %5llu/s", bar_char(n->rx_missed_s),
            (unsigned long long)n->rx_missed_s);
    wattroff(win, COLOR_PAIR(drop_color(n->rx_missed_s)));

    wattron(win, COLOR_PAIR(COL_NORMAL));
    wprintw(win, "  rx_fifo ");
    wattroff(win, COLOR_PAIR(COL_NORMAL));
    wattron(win, COLOR_PAIR(drop_color(n->rx_fifo_s)));
    wprintw(win, "%s %5llu/s", bar_char(n->rx_fifo_s),
            (unsigned long long)n->rx_fifo_s);
    wattroff(win, COLOR_PAIR(drop_color(n->rx_fifo_s)));
    r++;

    /* Ring buffer usage */
    wattron(win, COLOR_PAIR(COL_NORMAL));
    if (n->ring_max > 0) {
        int pct = (int)(n->ring_cur * 100 / n->ring_max);
        int filled = pct * 20 / 100;
        char bar[24] = {0};
        for (int i = 0; i < 20; i++) bar[i] = (i < filled) ? '#' : '.';
        mvwprintw(win, r, 0, " ring [%-20s] %u/%u (%d%%)  mcast_grp: %u",
                  bar, n->ring_cur, n->ring_max, pct, n->mcast_groups);
    } else {
        mvwprintw(win, r, 0, " ring [N/A]  mcast_grp: %u", n->mcast_groups);
    }
    wattroff(win, COLOR_PAIR(COL_NORMAL));
    r++;

    *row = r;
}

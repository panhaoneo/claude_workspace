#include <ncurses.h>
#include <string.h>
#include "panel_nic.h"
#include "store.h"

/* Format bytes/s to human-readable string */
static void fmt_bps(char *buf, size_t len, uint64_t bps)
{
    if (bps >= (uint64_t)1e9)
        snprintf(buf, len, "%.1f Gbps", bps / 1e9);
    else if (bps >= (uint64_t)1e6)
        snprintf(buf, len, "%.1f Mbps", bps / 1e6);
    else if (bps >= (uint64_t)1e3)
        snprintf(buf, len, "%.1f Kbps", bps / 1e3);
    else
        snprintf(buf, len, "%lu bps", (unsigned long)bps);
}

static void fmt_pps(char *buf, size_t len, uint64_t pps)
{
    if (pps >= (uint64_t)1e6)
        snprintf(buf, len, "%.1fM", pps / 1e6);
    else if (pps >= (uint64_t)1e3)
        snprintf(buf, len, "%.1fK", pps / 1e3);
    else
        snprintf(buf, len, "%lu", (unsigned long)pps);
}

void panel_nic_draw(WINDOW *win)
{
    int rows, cols;
    getmaxyx(win, rows, cols);
    (void)rows;

    werase(win);

    /* Header */
    wattron(win, A_BOLD | A_UNDERLINE);
    mvwprintw(win, 0, 0, "%-12s %10s %10s %14s %14s %8s %6s",
              "Interface", "RX PPS", "TX PPS", "RX BPS", "TX BPS",
              "DROP/s", "ERR/s");
    wattroff(win, A_BOLD | A_UNDERLINE);

    pthread_mutex_lock(&g_store.lock);

    int row = 1;
    for (int i = 0; i < g_store.nic_count && row < rows - 1; i++) {
        nic_stat_t *n = &g_store.nics[i];

        char rx_bps_s[20], tx_bps_s[20];
        char rx_pps_s[16], tx_pps_s[16];
        fmt_bps(rx_bps_s, sizeof(rx_bps_s), n->rx_bps * 8);
        fmt_bps(tx_bps_s, sizeof(tx_bps_s), n->tx_bps * 8);
        fmt_pps(rx_pps_s, sizeof(rx_pps_s), n->rx_pps);
        fmt_pps(tx_pps_s, sizeof(tx_pps_s), n->tx_pps);

        int selected = (i == g_store.selected_nic);
        if (selected)
            wattron(win, A_REVERSE | A_BOLD);

        if (n->drop_delta > 0)
            wattron(win, COLOR_PAIR(2)); /* yellow/red for drops */

        const char *prefix = selected ? "\xe2\x96\xb6 " : "  ";

        mvwprintw(win, row, 0, "%s%-10s %10s %10s %14s %14s %8lu %6lu",
                  prefix,
                  n->name,
                  rx_pps_s, tx_pps_s,
                  rx_bps_s, tx_bps_s,
                  (unsigned long)n->drop_delta,
                  (unsigned long)(n->rx_errors));

        /* Pad to full width */
        int cur_x = getcurx(win);
        for (int x = cur_x; x < cols; x++) waddch(win, ' ');

        if (n->drop_delta > 0)
            wattroff(win, COLOR_PAIR(2));
        if (selected)
            wattroff(win, A_REVERSE | A_BOLD);

        row++;
    }

    pthread_mutex_unlock(&g_store.lock);
    wrefresh(win);
}

void panel_nic_mouse(int click_y, int click_x)
{
    (void)click_x;
    /* Row 0 is header; data rows start at 1 */
    int nic_idx = click_y - 1;

    pthread_mutex_lock(&g_store.lock);
    if (nic_idx >= 0 && nic_idx < g_store.nic_count) {
        g_store.selected_nic = (g_store.selected_nic == nic_idx) ? -1 : nic_idx;
    }
    pthread_mutex_unlock(&g_store.lock);
}

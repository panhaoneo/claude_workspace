#include <ncurses.h>
#include <stdio.h>
#include "panel_flow.h"
#include "store.h"

static int selected_flow = -1;

static void fmt_kbps(char *buf, size_t len, uint64_t bps)
{
    double kbs = bps / 1024.0;
    if (kbs >= 1024.0 * 1024.0)
        snprintf(buf, len, "%.1f GB/s", kbs / (1024.0 * 1024.0));
    else if (kbs >= 1024.0)
        snprintf(buf, len, "%.1f MB/s", kbs / 1024.0);
    else
        snprintf(buf, len, "%.1f KB/s", kbs);
}

void panel_flow_draw(WINDOW *win)
{
    int rows, cols;
    getmaxyx(win, rows, cols);
    (void)cols;

    werase(win);

    wattron(win, A_BOLD | A_UNDERLINE);
    mvwprintw(win, 0, 0, "%-7s %-20s %12s %12s",
              "PID", "Name", "RX", "TX");
    wattroff(win, A_BOLD | A_UNDERLINE);

    pthread_mutex_lock(&g_store.lock);

    int row = 1;
    for (int i = 0; i < g_store.flow_count && row < rows - 1; i++) {
        flow_stat_t *f = &g_store.flows[i];

        /* Only show processes with traffic */
        if (f->rx_bps == 0 && f->tx_bps == 0 && i > 0) continue;

        char rx_s[20], tx_s[20];
        fmt_kbps(rx_s, sizeof(rx_s), f->rx_bps);
        fmt_kbps(tx_s, sizeof(tx_s), f->tx_bps);

        int sel = (i == selected_flow);
        if (sel)
            wattron(win, A_REVERSE | A_BOLD);

        mvwprintw(win, row, 0, "%-7d %-20.20s %12s %12s",
                  (int)f->pid, f->name, rx_s, tx_s);

        if (sel)
            wattroff(win, A_REVERSE | A_BOLD);

        row++;
    }

    pthread_mutex_unlock(&g_store.lock);
    wrefresh(win);
}

void panel_flow_mouse(int click_y, int click_x)
{
    (void)click_x;
    int idx = click_y - 1; /* header at row 0 */
    if (idx < 0) return;
    selected_flow = (selected_flow == idx) ? -1 : idx;
}

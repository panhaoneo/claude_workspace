/* render_kernel.c – Area B: Kernel / softnet rendering */
#include "render_kernel.h"
#include <stdio.h>

#define COL_NORMAL 1
#define COL_WARN   2
#define COL_CRIT   3
#define COL_OK     4
#define COL_HEADER 5

static int drop_color(uint64_t val) {
    if (val == 0) return COL_NORMAL;
    if (val < 100) return COL_WARN;
    return COL_CRIT;
}

static const char *bar_char(uint64_t val) {
    if (val == 0) return "\xe2\x96\x91";
    if (val < 100) return "\xe2\x96\x93";
    return "\xe2\x96\x88\xe2\x96\x88";
}

void render_kernel(WINDOW *win, const mwatch_store_t *store, int *row, int width) {
    const kernel_stat_t *k = &store->kernel;
    int r = *row;

    wattron(win, COLOR_PAIR(COL_HEADER) | A_BOLD);
    mvwprintw(win, r++, 0, "%-*s", width,
              "── KERNEL / SOFTNET ──────────────────────────────────────────────────");
    wattroff(win, COLOR_PAIR(COL_HEADER) | A_BOLD);

    /* softnet line */
    wattron(win, COLOR_PAIR(COL_NORMAL));
    mvwprintw(win, r, 0, " softnet  proc %9llu/s",
              (unsigned long long)k->sn_processed_s);
    wattroff(win, COLOR_PAIR(COL_NORMAL));

    wattron(win, COLOR_PAIR(COL_NORMAL));
    wprintw(win, "   drop ");
    wattroff(win, COLOR_PAIR(COL_NORMAL));
    wattron(win, COLOR_PAIR(drop_color(k->sn_dropped_s)));
    wprintw(win, "%s %6llu/s", bar_char(k->sn_dropped_s),
            (unsigned long long)k->sn_dropped_s);
    wattroff(win, COLOR_PAIR(drop_color(k->sn_dropped_s)));

    wattron(win, COLOR_PAIR(COL_NORMAL));
    wprintw(win, "   squeeze ");
    wattroff(win, COLOR_PAIR(COL_NORMAL));
    wattron(win, COLOR_PAIR(drop_color(k->sn_squeezed_s)));
    wprintw(win, "%s %6llu/s", bar_char(k->sn_squeezed_s),
            (unsigned long long)k->sn_squeezed_s);
    wattroff(win, COLOR_PAIR(drop_color(k->sn_squeezed_s)));
    r++;

    /* UDP line */
    wattron(win, COLOR_PAIR(COL_NORMAL));
    mvwprintw(win, r, 0, " UDP      rx   %9llu/s",
              (unsigned long long)k->udp_in_s);
    wattroff(win, COLOR_PAIR(COL_NORMAL));

    wattron(win, COLOR_PAIR(COL_NORMAL));
    wprintw(win, "   RcvbufErr ");
    wattroff(win, COLOR_PAIR(COL_NORMAL));
    wattron(win, COLOR_PAIR(drop_color(k->udp_rcvbuf_err_s)));
    wprintw(win, "%s %6llu/s", bar_char(k->udp_rcvbuf_err_s),
            (unsigned long long)k->udp_rcvbuf_err_s);
    wattroff(win, COLOR_PAIR(drop_color(k->udp_rcvbuf_err_s)));

    wattron(win, COLOR_PAIR(COL_NORMAL));
    wprintw(win, "   InErr ");
    wattroff(win, COLOR_PAIR(COL_NORMAL));
    wattron(win, COLOR_PAIR(drop_color(k->udp_in_err_s)));
    wprintw(win, "%s %6llu/s", bar_char(k->udp_in_err_s),
            (unsigned long long)k->udp_in_err_s);
    wattroff(win, COLOR_PAIR(drop_color(k->udp_in_err_s)));
    r++;

    /* params line */
    wattron(win, COLOR_PAIR(COL_NORMAL));
    mvwprintw(win, r++, 0,
              " params   rmem_max: %u (%uKB)   netdev_backlog: %u",
              k->rmem_max, k->rmem_max / 1024, k->netdev_max_backlog);
    wattroff(win, COLOR_PAIR(COL_NORMAL));

    *row = r;
}

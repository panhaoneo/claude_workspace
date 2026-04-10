/* render_ebpf.c – Area E: eBPF drop reason */
#include "render_ebpf.h"
#include <stdio.h>
#include <string.h>

#define COL_NORMAL 1
#define COL_WARN   2
#define COL_CRIT   3
#define COL_OK     4
#define COL_HEADER 5

void render_ebpf(WINDOW *win, const mwatch_store_t *store, int *row, int width,
                 int collapsed)
{
    int r = *row;

    wattron(win, COLOR_PAIR(COL_HEADER) | A_BOLD);
    if (collapsed) {
        mvwprintw(win, r++, 0, "%-*s", width,
                  "-- eBPF DROP REASON [collapsed - press 'e' to expand] --------------");
        wattroff(win, COLOR_PAIR(COL_HEADER) | A_BOLD);
        *row = r;
        return;
    }
    mvwprintw(win, r++, 0, "%-*s", width,
              "-- eBPF DROP REASON ---------------------------------------------------");
    wattroff(win, COLOR_PAIR(COL_HEADER) | A_BOLD);

    const ebpf_stat_t *e = &store->ebpf;

    if (!e->enabled) {
        wattron(win, COLOR_PAIR(COL_WARN));
        mvwprintw(win, r++, 0,
                  " [eBPF: disabled - run as root with BTF enabled]");
        wattroff(win, COLOR_PAIR(COL_WARN));
        *row = r;
        return;
    }

    if (e->reason_count == 0) {
        wattron(win, COLOR_PAIR(COL_NORMAL));
        mvwprintw(win, r++, 0, " (no drop events yet)");
        wattroff(win, COLOR_PAIR(COL_NORMAL));
        *row = r;
        return;
    }

    int shown = 0;
    for (int i = 0; i < e->reason_count && shown < 8; i++) {
        const drop_reason_stat_t *dr = &e->reasons[i];
        if (dr->count == 0) continue;

        int col = (dr->count_s > 100) ? COL_CRIT :
                  (dr->count_s >   0) ? COL_WARN  : COL_NORMAL;
        const char *bar = (dr->count_s == 0) ? " " :
                          (dr->count_s < 100) ? "~" : "#";
        const char *note = "";
        if (dr->count_s > 0 && strstr(dr->reason_str, "UDP_SOCK_FULL"))
            note = " <- primary drop reason";

        wattron(win, COLOR_PAIR(col));
        mvwprintw(win, r++, 0, " %-40s %s %6llu/s%s",
                  dr->reason_str, bar,
                  (unsigned long long)dr->count_s, note);
        wattroff(win, COLOR_PAIR(col));
        shown++;
    }

    *row = r;
}

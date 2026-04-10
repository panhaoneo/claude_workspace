/* render_socket.c – Area D: per-socket UDP drops */
#include "render_socket.h"
#include <stdio.h>
#include <string.h>

#define COL_NORMAL 1
#define COL_WARN   2
#define COL_CRIT   3
#define COL_OK     4
#define COL_HEADER 5

void render_socket(WINDOW *win, const mwatch_store_t *store, int *row, int width) {
    (void)width;
    int r = *row;

    wattron(win, COLOR_PAIR(COL_HEADER) | A_BOLD);
    mvwprintw(win, r++, 0, "%-*s", width,
              "-- SOCKET (UDP drops) -------------------------------------------------");
    wattroff(win, COLOR_PAIR(COL_HEADER) | A_BOLD);

    if (store->socket_count == 0) {
        wattron(win, COLOR_PAIR(COL_NORMAL));
        mvwprintw(win, r++, 0, " (no UDP sockets found)");
        wattroff(win, COLOR_PAIR(COL_NORMAL));
        *row = r;
        return;
    }

    /* Header */
    wattron(win, COLOR_PAIR(COL_NORMAL) | A_BOLD);
    mvwprintw(win, r++, 0, " %-22s %-16s %8s  %s",
              "Local Addr", "Proc", "rcvbuf", "drops/s");
    wattroff(win, COLOR_PAIR(COL_NORMAL) | A_BOLD);

    int shown = 0;
    for (int i = 0; i < store->socket_count; i++) {
        const socket_stat_t *s = &store->sockets[i];
        /* Only show sockets with drops or non-zero rcvbuf */
        if (s->drops_s == 0 && s->drops == 0 && s->rcvbuf == 0) continue;
        if (shown >= 6) break;   /* cap display rows */

        int col = (s->drops_s > 0) ? (s->drops_s > 100 ? COL_CRIT : COL_WARN) : COL_NORMAL;
        const char *bar = (s->drops_s == 0) ? " " :
                          (s->drops_s < 100) ? "~" : "#";

        wattron(win, COLOR_PAIR(COL_NORMAL));
        mvwprintw(win, r, 0, " %-22s %-16s %8u  ",
                  s->local_addr,
                  s->proc_name[0] ? s->proc_name : "?",
                  s->rcvbuf);
        wattroff(win, COLOR_PAIR(COL_NORMAL));

        wattron(win, COLOR_PAIR(col));
        wprintw(win, "%s %6llu/s", bar, (unsigned long long)s->drops_s);
        wattroff(win, COLOR_PAIR(col));
        r++;
        shown++;
    }
    if (shown == 0) {
        wattron(win, COLOR_PAIR(COL_OK));
        mvwprintw(win, r++, 0, " OK no per-socket drops");
        wattroff(win, COLOR_PAIR(COL_OK));
    }

    *row = r;
}

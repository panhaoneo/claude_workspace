/* render_diag.c – Area F: DIAGNOSIS */
#include "render_diag.h"
#include <stdio.h>
#include <string.h>

#define COL_NORMAL 1
#define COL_WARN   2
#define COL_CRIT   3
#define COL_OK     4
#define COL_HEADER 5

void render_diag(WINDOW *win, const mwatch_store_t *store, int *row, int width) {
    int r = *row;

    wattron(win, COLOR_PAIR(COL_HEADER) | A_BOLD);
    mvwprintw(win, r++, 0, "%-*s", width,
              "── DIAGNOSIS ─────────────────────────────────────────────────────────");
    wattroff(win, COLOR_PAIR(COL_HEADER) | A_BOLD);

    if (store->diag_count == 0) {
        wattron(win, COLOR_PAIR(COL_NORMAL));
        mvwprintw(win, r++, 0, " (no data yet)");
        wattroff(win, COLOR_PAIR(COL_NORMAL));
        *row = r;
        return;
    }

    for (int i = 0; i < store->diag_count; i++) {
        const diag_item_t *d = &store->diag[i];
        const char *icon;
        int col;

        switch (d->level) {
        case DIAG_CRIT:
            icon = "\xf0\x9f\x94\xa5";  /* 🔥 */
            col  = COL_CRIT;
            break;
        case DIAG_WARN:
            icon = "\xe2\x9a\xa0";      /* ⚠ */
            col  = COL_WARN;
            break;
        default:
            icon = "\xe2\x9c\x93";      /* ✓ */
            col  = COL_OK;
            break;
        }

        wattron(win, COLOR_PAIR(col));
        mvwprintw(win, r, 0, " %s %-16s  %s", icon, d->layer, d->summary);
        wattroff(win, COLOR_PAIR(col));

        if (d->cmd[0]) {
            wattron(win, COLOR_PAIR(COL_NORMAL));
            wprintw(win, " → %s", d->cmd);
            wattroff(win, COLOR_PAIR(COL_NORMAL));
        }
        r++;
    }

    *row = r;
}

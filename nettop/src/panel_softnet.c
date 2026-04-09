#include <ncurses.h>
#include "panel_softnet.h"
#include "store.h"

void panel_softnet_draw(WINDOW *win)
{
    int rows, cols;
    getmaxyx(win, rows, cols);
    (void)cols;

    werase(win);

    wattron(win, A_BOLD | A_UNDERLINE);
    mvwprintw(win, 0, 0, "%-6s %14s %12s %16s",
              "CPU", "processed", "dropped", "time_squeezed");
    wattroff(win, A_BOLD | A_UNDERLINE);

    pthread_mutex_lock(&g_store.lock);

    int row = 1;
    for (int i = 0; i < g_store.cpu_count && row < rows - 1; i++) {
        softnet_stat_t *s = &g_store.softnet[i];

        int squeezed_alert = (s->squeezed_delta > 0);

        mvwprintw(win, row, 0, "CPU%-3d %14lu %12lu ",
                  i,
                  (unsigned long)s->processed,
                  (unsigned long)s->dropped);

        if (squeezed_alert)
            wattron(win, COLOR_PAIR(2) | A_BOLD);

        wprintw(win, "%16lu", (unsigned long)s->time_squeezed);

        if (squeezed_alert) {
            wprintw(win, " [+%lu]", (unsigned long)s->squeezed_delta);
            wattroff(win, COLOR_PAIR(2) | A_BOLD);
        }

        row++;
    }

    pthread_mutex_unlock(&g_store.lock);
    wrefresh(win);
}

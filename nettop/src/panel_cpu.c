#include <ncurses.h>
#include <stdio.h>
#include "panel_cpu.h"
#include "store.h"

#define BAR_WIDTH 20

void panel_cpu_draw(WINDOW *win)
{
    int rows, cols;
    getmaxyx(win, rows, cols);
    (void)cols;

    werase(win);

    wattron(win, A_BOLD | A_UNDERLINE);
    mvwprintw(win, 0, 0, "%-6s  %-22s %%CPU   softirq%%", "CPU", "Usage");
    wattroff(win, A_BOLD | A_UNDERLINE);

    pthread_mutex_lock(&g_store.lock);

    int row = 1;
    for (int i = 0; i < g_store.cpu_count && row < rows - 1; i++) {
        cpu_stat_t *c = &g_store.cpus[i];

        int filled = (int)(c->usage_pct * BAR_WIDTH / 100.0);
        if (filled > BAR_WIDTH) filled = BAR_WIDTH;
        if (filled < 0) filled = 0;

        char bar[BAR_WIDTH + 1];
        for (int b = 0; b < BAR_WIDTH; b++)
            bar[b] = (b < filled) ? '#' : ' ';
        bar[BAR_WIDTH] = '\0';

        int softirq_alert = (c->softirq_pct > 30.0);

        mvwprintw(win, row, 0, "CPU%-3d [", i);
        if (c->usage_pct > 80.0)
            wattron(win, COLOR_PAIR(2) | A_BOLD);
        else if (c->usage_pct > 50.0)
            wattron(win, COLOR_PAIR(3));
        wprintw(win, "%s", bar);
        if (c->usage_pct > 80.0)
            wattroff(win, COLOR_PAIR(2) | A_BOLD);
        else if (c->usage_pct > 50.0)
            wattroff(win, COLOR_PAIR(3));

        wprintw(win, "] %5.1f%%", c->usage_pct);

        wprintw(win, "   softirq: ");
        if (softirq_alert)
            wattron(win, COLOR_PAIR(2) | A_BOLD);
        wprintw(win, "%5.1f%%", c->softirq_pct);
        if (softirq_alert) {
            wprintw(win, " !");
            wattroff(win, COLOR_PAIR(2) | A_BOLD);
        }

        row++;
    }

    pthread_mutex_unlock(&g_store.lock);
    wrefresh(win);
}

#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include "panel_irq.h"
#include "store.h"

/* 5-level block characters for the heatmap */
static const char *heat_chars[] = {
    " ", "\u2591", "\u2593", "\u2588", "\u2588\u2588"
};
static const char **heat = NULL;

static void init_heat(void)
{
    if (heat) return;
    /* Use UTF-8 blocks */
    heat = heat_chars;
}

/* Map a delta value to heat level 0-4 given max in row */
static int heat_level(uint64_t val, uint64_t max)
{
    if (max == 0 || val == 0) return 0;
    double ratio = (double)val / (double)max;
    if (ratio < 0.20) return 1;
    if (ratio < 0.40) return 2;
    if (ratio < 0.80) return 3;
    return 4;
}

void panel_irq_draw(WINDOW *win)
{
    int rows, cols;
    getmaxyx(win, rows, cols);

    init_heat();
    werase(win);

    pthread_mutex_lock(&g_store.lock);

    /* Title: show selected NIC name or "all" */
    const char *nic_name = "all";
    if (g_store.selected_nic >= 0 &&
        g_store.selected_nic < g_store.nic_count) {
        nic_name = g_store.nics[g_store.selected_nic].name;
    }

    wattron(win, A_BOLD);
    mvwprintw(win, 0, 0, "IRQ Distribution — %s", nic_name);
    wattroff(win, A_BOLD);

    /* How many CPUs fit across the screen? */
    int ncpus = g_store.cpu_count;
    if (ncpus == 0) {
        pthread_mutex_unlock(&g_store.lock);
        wrefresh(win);
        return;
    }

    /* Column layout: "queueXX  " prefix = 10 chars, each CPU = 5 chars */
    int prefix_w = 10;
    int cpu_w    = 5;
    int max_cpus_fit = (cols - prefix_w) / cpu_w;
    if (max_cpus_fit < 1) max_cpus_fit = 1;
    if (max_cpus_fit > ncpus) max_cpus_fit = ncpus;

    /* Header row: CPU numbers */
    mvwprintw(win, 1, 0, "%-10s", "");
    for (int c = 0; c < max_cpus_fit; c++) {
        mvwprintw(win, 1, prefix_w + c * cpu_w, "CPU%-2d", c);
    }
    wattron(win, A_UNDERLINE);
    mvwprintw(win, 2, 0, "%*s", cols, "");
    wattroff(win, A_UNDERLINE);

    int row = 2;
    for (int i = 0; i < g_store.irq_count && row < rows - 2; i++) {
        irq_stat_t *irq = &g_store.irqs[i];

        /* Filter by selected NIC if one is selected */
        if (g_store.selected_nic >= 0) {
            if (strstr(irq->name,
                       g_store.nics[g_store.selected_nic].name) == NULL) {
                continue;
            }
        }

        /* Find max delta across CPUs for this IRQ row */
        uint64_t row_max = 0;
        for (int c = 0; c < ncpus; c++) {
            if (irq->delta[c] > row_max) row_max = irq->delta[c];
        }

        /* Shorten name to prefix_w-1 chars */
        char short_name[12];
        snprintf(short_name, sizeof(short_name), "%-9.9s", irq->name);
        mvwprintw(win, row, 0, "%s ", short_name);

        for (int c = 0; c < max_cpus_fit; c++) {
            int lvl = heat_level(irq->delta[c], row_max);

            int col_start = prefix_w + c * cpu_w;
            if (col_start + 4 >= cols) break;

            /* Color by level */
            if (lvl >= 4)
                wattron(win, COLOR_PAIR(2) | A_BOLD);
            else if (lvl == 3)
                wattron(win, COLOR_PAIR(2));
            else if (lvl == 2)
                wattron(win, COLOR_PAIR(3));
            else if (lvl == 1)
                wattron(win, COLOR_PAIR(4));

            mvwprintw(win, row, col_start, " %-3s ", heat[lvl]);

            if (lvl >= 3)
                wattroff(win, COLOR_PAIR(2) | A_BOLD);
            else if (lvl == 2)
                wattroff(win, COLOR_PAIR(2));
            else if (lvl == 2)
                wattroff(win, COLOR_PAIR(3));
            else if (lvl == 1)
                wattroff(win, COLOR_PAIR(4));
        }
        row++;
    }

    /* Legend */
    if (row < rows - 1) {
        mvwprintw(win, row + 1, 0,
                  "Legend: %s(0) %s(low) %s(mid) %s(high) %s(max)",
                  heat[0], heat[1], heat[2], heat[3], heat[4]);
    }

    pthread_mutex_unlock(&g_store.lock);
    wrefresh(win);
}

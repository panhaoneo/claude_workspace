/* render_cpu.c – Area C: CPU & IRQ rendering */
#include "render_cpu.h"
#include <stdio.h>
#include <string.h>

#define COL_NORMAL 1
#define COL_WARN   2
#define COL_CRIT   3
#define COL_OK     4
#define COL_HEADER 5

#define MAX_DISPLAY_CPUS 8   /* cap displayed CPUs to avoid overflow */

static int pct_color(double pct) {
    if (pct < 20.0) return COL_NORMAL;
    if (pct < 30.0) return COL_WARN;
    return COL_CRIT;
}

/* Draw a 10-char bar for a percentage */
static void draw_pct_bar(WINDOW *win, double pct, int color_pair) {
    int filled = (int)(pct / 10.0);
    if (filled > 10) filled = 10;
    char bar[11];
    for (int i = 0; i < 10; i++) bar[i] = (i < filled) ? '#' : '.';
    bar[10] = '\0';
    wattron(win, COLOR_PAIR(color_pair));
    wprintw(win, "[%s]", bar);
    wattroff(win, COLOR_PAIR(color_pair));
}

void render_cpu(WINDOW *win, const mwatch_store_t *store, int *row, int width) {
    (void)width;
    int r = *row;

    wattron(win, COLOR_PAIR(COL_HEADER) | A_BOLD);
    mvwprintw(win, r++, 0, "%-*s", width,
              "── CPU & IRQ ─────────────────────────────────────────────────────────");
    wattroff(win, COLOR_PAIR(COL_HEADER) | A_BOLD);

    int n = store->cpu_count;
    if (n > MAX_DISPLAY_CPUS) n = MAX_DISPLAY_CPUS;

    for (int i = 0; i < n; i++) {
        const cpu_stat_t *c = &store->cpus[i];
        int uc = pct_color(c->usage_pct);
        int sc = pct_color(c->softirq_pct);

        wattron(win, COLOR_PAIR(COL_NORMAL));
        mvwprintw(win, r, 0, " CPU%-2d ", i);
        wattroff(win, COLOR_PAIR(COL_NORMAL));

        draw_pct_bar(win, c->usage_pct, uc);

        wattron(win, COLOR_PAIR(uc));
        wprintw(win, " %3.0f%%", c->usage_pct);
        wattroff(win, COLOR_PAIR(uc));

        wattron(win, COLOR_PAIR(COL_NORMAL));
        wprintw(win, "  NET_RX %7llu/s",
                (unsigned long long)c->net_rx_sirq_s);
        wattroff(win, COLOR_PAIR(COL_NORMAL));

        wattron(win, COLOR_PAIR(COL_NORMAL));
        wprintw(win, "  sirq ");
        wattroff(win, COLOR_PAIR(COL_NORMAL));
        wattron(win, COLOR_PAIR(sc));
        wprintw(win, "%3.0f%%", c->softirq_pct);
        wattroff(win, COLOR_PAIR(sc));

        r++;
    }
    if (store->cpu_count > MAX_DISPLAY_CPUS) {
        wattron(win, COLOR_PAIR(COL_NORMAL));
        mvwprintw(win, r++, 0, " ... (%d more CPUs)", store->cpu_count - MAX_DISPLAY_CPUS);
        wattroff(win, COLOR_PAIR(COL_NORMAL));
    }

    /* IRQ distribution line */
    const irq_stat_t *irq = &store->irq;
    if (irq->queue_count > 0) {
        wattron(win, COLOR_PAIR(COL_NORMAL));
        mvwprintw(win, r, 0, " IRQ ");
        wattroff(win, COLOR_PAIR(COL_NORMAL));

        int max_q = irq->queue_count < 4 ? irq->queue_count : 4;
        for (int q = 0; q < max_q; q++) {
            const irq_queue_t *qu = &irq->queues[q];
            /* find dominant CPU */
            int dom_cpu = 0;
            uint64_t dom_cnt = 0;
            for (int c = 0; c < irq->cpu_count && c < MAX_CPUS; c++) {
                if (qu->cpu_counts[c] > dom_cnt) {
                    dom_cnt = qu->cpu_counts[c];
                    dom_cpu = c;
                }
            }
            int pct = (qu->total > 0) ? (int)(dom_cnt * 100 / qu->total) : 0;
            int col = (pct > 80) ? COL_CRIT : (pct > 50 ? COL_WARN : COL_NORMAL);
            wattron(win, COLOR_PAIR(col));
            wprintw(win, "%s→CPU%d(%d%%) ", qu->queue_name, dom_cpu, pct);
            wattroff(win, COLOR_PAIR(col));
        }
        r++;
    }

    /* irqbalance status */
    if (irq->irqbalance_running) {
        wattron(win, COLOR_PAIR(COL_WARN));
        mvwprintw(win, r++, 0, " irqbalance: running \xe2\x86\x90 WARN");
        wattroff(win, COLOR_PAIR(COL_WARN));
    }

    *row = r;
}

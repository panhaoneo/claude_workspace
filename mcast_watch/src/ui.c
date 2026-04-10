/* ui.c – ncurses single-screen UI loop */
#include "ui.h"
#include "render_nic.h"
#include <locale.h>
#include "render_kernel.h"
#include "render_cpu.h"
#include "render_socket.h"
#include "render_ebpf.h"
#include "render_diag.h"
#include "alert.h"
#include <curses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Colour pair IDs (exported via macros in render_*.c) */
#define COL_NORMAL  1
#define COL_WARN    2
#define COL_CRIT    3
#define COL_OK      4
#define COL_HEADER  5

#ifndef VERSION
#define VERSION "1.1"
#endif

static void init_colors(void) {
    start_color();
    use_default_colors();
    init_pair(COL_NORMAL, COLOR_WHITE,   -1);
    init_pair(COL_WARN,   COLOR_YELLOW,  -1);
    init_pair(COL_CRIT,   COLOR_RED,     -1);
    init_pair(COL_OK,     COLOR_GREEN,   -1);
    init_pair(COL_HEADER, COLOR_CYAN,    -1);
}

static void draw_title_bar(WINDOW *win, const mwatch_store_t *store,
                           int ebpf_on, int width) {
    char title[256];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[16];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    snprintf(title, sizeof(title),
             " mcast_watch v%s  if: %s  [eBPF:%s]  %s        q:quit r:refresh c:clear e:ebpf",
             VERSION, store->ifname,
             ebpf_on ? "ON" : "OFF",
             ts);

    wattron(win, COLOR_PAIR(COL_HEADER) | A_REVERSE | A_BOLD);
    mvwprintw(win, 0, 0, "%-*.*s", width, width, title);
    wattroff(win, COLOR_PAIR(COL_HEADER) | A_REVERSE | A_BOLD);
}

static void redraw(WINDOW *win, const mwatch_store_t *store,
                   int ebpf_collapsed, int width, int height) {
    (void)height;
    werase(win);

    int row = 0;

    draw_title_bar(win, store, store->ebpf.enabled, width);
    row = 1;

    render_nic(win, store, &row, width);
    render_kernel(win, store, &row, width);
    render_cpu(win, store, &row, width);
    render_socket(win, store, &row, width);
    render_ebpf(win, store, &row, width, ebpf_collapsed);
    render_diag(win, store, &row, width);

    wrefresh(win);
}

void ui_run(mwatch_store_t *store, volatile int *running) {
    setlocale(LC_ALL, "");   /* must be before initscr() */
    WINDOW *win = initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(win, TRUE);
    wtimeout(win, 200);   /* 200 ms getch timeout */

    if (has_colors()) init_colors();

    int ebpf_collapsed = 0;
    uint64_t last_tick = UINT64_MAX;

    while (*running) {
        int ch = wgetch(win);

        switch (ch) {
        case 'q': case 'Q':
            *running = 0;
            break;
        case 'r': case 'R':
            last_tick = UINT64_MAX - 1;   /* force redraw */
            break;
        case 'c': case 'C':
            alert_clear();
            pthread_mutex_lock(&store->lock);
            store->ebpf.reason_count = 0;
            store->diag_count = 0;
            pthread_mutex_unlock(&store->lock);
            break;
        case 'e': case 'E':
            ebpf_collapsed = !ebpf_collapsed;
            last_tick = UINT64_MAX - 1;
            break;
        case KEY_RESIZE:
            last_tick = UINT64_MAX - 1;
            break;
        default:
            break;
        }

        /* Redraw when store tick changes */
        pthread_mutex_lock(&store->lock);
        uint64_t cur_tick = store->tick;
        mwatch_store_t snap = *store;
        pthread_mutex_unlock(&store->lock);

        if (cur_tick != last_tick) {
            int h = LINES, w = COLS;
            redraw(win, &snap, ebpf_collapsed, w, h);
            last_tick = cur_tick;
        }
    }

    endwin();
}

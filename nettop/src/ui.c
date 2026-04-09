#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "ui.h"
#include "store.h"
#include "panel_nic.h"
#include "panel_cpu.h"
#include "panel_irq.h"
#include "panel_softnet.h"
#include "panel_sock.h"
#include "panel_flow.h"
#include "collector.h"

/* ------------------------------------------------------------------ */
/* Color pair definitions                                               */
/*   1 = normal highlight (cyan)                                        */
/*   2 = alert / danger  (red on default)                              */
/*   3 = warning         (yellow on default)                            */
/*   4 = low             (green on default)                             */
/* ------------------------------------------------------------------ */
#define CP_HIGHLIGHT  1
#define CP_ALERT      2
#define CP_WARN       3
#define CP_OK         4

static tab_t   current_tab = TAB_NIC;
static WINDOW *tab_win     = NULL;  /* top tab bar */
static WINDOW *content_win = NULL;  /* main content area */
static WINDOW *alert_win   = NULL;  /* bottom alert bar */

static const char *tab_labels[TAB_COUNT] = {
    "NIC", "CPU", "IRQ", "SOFTNET", "SOCK", "FLOW"
};

/* ------------------------------------------------------------------ */
/* Tab bar geometry helpers                                             */
/* ------------------------------------------------------------------ */

/* Return the start column of tab i in the tab bar */
static int tab_col(int i)
{
    /* "nettop v1.0  " = 13 chars prefix */
    int col = 13;
    for (int t = 0; t < i; t++) {
        col += (int)strlen(tab_labels[t]) + 4; /* "[LABEL] " */
    }
    return col;
}

/* ------------------------------------------------------------------ */
/* Draw the top tab bar                                                  */
/* ------------------------------------------------------------------ */
static void draw_tabbar(void)
{
    int cols = getmaxx(tab_win);
    werase(tab_win);
    wattron(tab_win, A_BOLD | COLOR_PAIR(CP_HIGHLIGHT));
    mvwprintw(tab_win, 0, 0, " nettop v1.0  ");
    wattroff(tab_win, COLOR_PAIR(CP_HIGHLIGHT));

    for (int t = 0; t < TAB_COUNT; t++) {
        int col = tab_col(t);
        if (col + (int)strlen(tab_labels[t]) + 3 >= cols) break;
        if ((tab_t)t == current_tab) {
            wattron(tab_win, A_REVERSE);
        }
        wprintw(tab_win, "[%s] ", tab_labels[t]);
        if ((tab_t)t == current_tab) {
            wattroff(tab_win, A_REVERSE);
        }
    }
    wattroff(tab_win, A_BOLD);

    /* Right-align 'q' hint */
    int hint_col = cols - 4;
    if (hint_col > 0)
        mvwprintw(tab_win, 0, hint_col, " q  ");

    wrefresh(tab_win);
}

/* ------------------------------------------------------------------ */
/* Draw the alert bar                                                    */
/* ------------------------------------------------------------------ */
static void draw_alertbar(void)
{
    int cols = getmaxx(alert_win);
    werase(alert_win);

    pthread_mutex_lock(&g_store.lock);

    if (g_store.alert_count > 0) {
        alert_t *a = &g_store.alerts[0];
        wattron(alert_win, COLOR_PAIR(CP_ALERT) | A_BOLD);
        mvwprintw(alert_win, 0, 0, "[WARN] %s %.*s",
                  a->time_str, cols - 20, a->msg);
        wattroff(alert_win, COLOR_PAIR(CP_ALERT) | A_BOLD);
    } else {
        wattron(alert_win, COLOR_PAIR(CP_OK));
        mvwprintw(alert_win, 0, 0, " No alerts");
        wattroff(alert_win, COLOR_PAIR(CP_OK));
    }

    pthread_mutex_unlock(&g_store.lock);
    wrefresh(alert_win);
}

/* ------------------------------------------------------------------ */
/* Dispatch draw for the current tab                                     */
/* ------------------------------------------------------------------ */
static void draw_content(void)
{
    switch (current_tab) {
    case TAB_NIC:     panel_nic_draw(content_win);     break;
    case TAB_CPU:     panel_cpu_draw(content_win);     break;
    case TAB_IRQ:     panel_irq_draw(content_win);     break;
    case TAB_SOFTNET: panel_softnet_draw(content_win); break;
    case TAB_SOCK:    panel_sock_draw(content_win);    break;
    case TAB_FLOW:    panel_flow_draw(content_win);    break;
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* Handle a mouse click on the tab bar                                  */
/* ------------------------------------------------------------------ */
static void handle_tabbar_click(int click_x)
{
    for (int t = 0; t < TAB_COUNT; t++) {
        int start = tab_col(t);
        int end   = start + (int)strlen(tab_labels[t]) + 2; /* "[LABEL]" */
        if (click_x >= start && click_x < end) {
            current_tab = (tab_t)t;
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* ui_init                                                              */
/* ------------------------------------------------------------------ */
void ui_init(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    /* Enable mouse */
    mousemask(BUTTON1_CLICKED | BUTTON1_PRESSED, NULL);
    mouseinterval(0);

    /* Colors */
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(CP_HIGHLIGHT, COLOR_CYAN,    -1);
        init_pair(CP_ALERT,     COLOR_RED,     -1);
        init_pair(CP_WARN,      COLOR_YELLOW,  -1);
        init_pair(CP_OK,        COLOR_GREEN,   -1);
    }

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    /* Tab bar: 1 row at top */
    tab_win     = newwin(1, cols, 0, 0);
    /* Content: rows - 2 lines (minus tab bar and alert bar) */
    content_win = newwin(rows - 2, cols, 1, 0);
    /* Alert bar: 1 row at bottom */
    alert_win   = newwin(1, cols, rows - 1, 0);

    /* getch timeout for 100ms polling */
    wtimeout(stdscr, 100);
}

/* ------------------------------------------------------------------ */
/* ui_cleanup                                                           */
/* ------------------------------------------------------------------ */
void ui_cleanup(void)
{
    if (content_win) { delwin(content_win); content_win = NULL; }
    if (tab_win)     { delwin(tab_win);     tab_win     = NULL; }
    if (alert_win)   { delwin(alert_win);   alert_win   = NULL; }
    endwin();
}

/* ------------------------------------------------------------------ */
/* Resize handler                                                        */
/* ------------------------------------------------------------------ */
static void handle_resize(void)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    if (content_win) delwin(content_win);
    if (tab_win)     delwin(tab_win);
    if (alert_win)   delwin(alert_win);

    tab_win     = newwin(1, cols, 0, 0);
    content_win = newwin(rows - 2, cols, 1, 0);
    alert_win   = newwin(1, cols, rows - 1, 0);

    clear();
    refresh();
}

/* ------------------------------------------------------------------ */
/* ui_run — main UI event loop                                          */
/* ------------------------------------------------------------------ */
void ui_run(void)
{
    uint64_t last_tick = (uint64_t)-1;

    while (1) {
        /* Check if store has new data */
        pthread_mutex_lock(&g_store.lock);
        uint64_t cur_tick = g_store.tick;
        pthread_mutex_unlock(&g_store.lock);

        if (cur_tick != last_tick) {
            last_tick = cur_tick;
            draw_tabbar();
            draw_content();
            draw_alertbar();
        }

        int ch = wgetch(stdscr);
        if (ch == 'q' || ch == 'Q') break;

        if (ch == KEY_RESIZE) {
            handle_resize();
            draw_tabbar();
            draw_content();
            draw_alertbar();
            continue;
        }

        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                int tab_y  = 0;
                int cont_y = 1;
                int cont_h = getmaxy(content_win);

                if (ev.y == tab_y) {
                    /* Click on tab bar */
                    handle_tabbar_click(ev.x);
                    draw_tabbar();
                    draw_content();
                } else if (ev.y >= cont_y &&
                           ev.y < cont_y + cont_h) {
                    /* Click inside content */
                    int local_y = ev.y - cont_y;
                    switch (current_tab) {
                    case TAB_NIC:
                        panel_nic_mouse(local_y, ev.x);
                        break;
                    case TAB_FLOW:
                        panel_flow_mouse(local_y, ev.x);
                        break;
                    default:
                        break;
                    }
                    draw_content();
                }
            }
        }
    }

    collector_stop();
}

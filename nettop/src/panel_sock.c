#include <ncurses.h>
#include "panel_sock.h"
#include "store.h"

void panel_sock_draw(WINDOW *win)
{
    int rows, cols;
    getmaxyx(win, rows, cols);
    (void)rows;
    (void)cols;

    werase(win);

    pthread_mutex_lock(&g_store.lock);
    sock_stat_t s = g_store.sock;
    pthread_mutex_unlock(&g_store.lock);

    wattron(win, A_BOLD);
    mvwprintw(win, 0, 0, "Socket Statistics");
    wattroff(win, A_BOLD);

    int row = 2;
    mvwprintw(win, row++, 2, "TCP ESTABLISHED : %u", s.tcp_estab);
    mvwprintw(win, row++, 2, "TCP TIME_WAIT   : %u", s.tcp_time_wait);
    mvwprintw(win, row++, 2, "TCP CLOSE_WAIT  : %u", s.tcp_close_wait);
    mvwprintw(win, row++, 2, "UDP in use      : %u", s.udp_inuse);

    row++;
    mvwprintw(win, row++, 2, "TCP RetransSegs : %lu (delta: +%lu)",
              (unsigned long)s.tcp_retrans,
              (unsigned long)s.retrans_delta);

    if (s.retrans_delta > 0) {
        wattron(win, COLOR_PAIR(2) | A_BOLD);
        mvwprintw(win, row++, 2, "[WARN] TCP retransmit activity detected!");
        wattroff(win, COLOR_PAIR(2) | A_BOLD);
    }

    wrefresh(win);
}

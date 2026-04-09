#ifndef PANEL_FLOW_H
#define PANEL_FLOW_H

#include <ncurses.h>

void panel_flow_draw(WINDOW *win);
void panel_flow_mouse(int y, int x);

#endif /* PANEL_FLOW_H */

/* render_socket.h – Area D: Socket (UDP per-socket drops) */
#pragma once
#include <curses.h>
#include "store.h"

void render_socket(WINDOW *win, const mwatch_store_t *store, int *row, int width);

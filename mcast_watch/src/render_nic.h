/* render_nic.h – Area A: NIC & Driver */
#pragma once
#include <curses.h>
#include "store.h"

/* Draw NIC section starting at *row; advances *row past drawn content. */
void render_nic(WINDOW *win, const mwatch_store_t *store, int *row, int width);

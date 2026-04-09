/* render_kernel.h – Area B: Kernel / softnet */
#pragma once
#include <curses.h>
#include "store.h"

void render_kernel(WINDOW *win, const mwatch_store_t *store, int *row, int width);

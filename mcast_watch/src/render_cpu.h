/* render_cpu.h – Area C: CPU & IRQ */
#pragma once
#include <curses.h>
#include "store.h"

void render_cpu(WINDOW *win, const mwatch_store_t *store, int *row, int width);

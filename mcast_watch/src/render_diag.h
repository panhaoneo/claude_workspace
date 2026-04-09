/* render_diag.h – Area F: DIAGNOSIS */
#pragma once
#include <curses.h>
#include "store.h"

void render_diag(WINDOW *win, const mwatch_store_t *store, int *row, int width);

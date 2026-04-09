/* render_ebpf.h – Area E: eBPF drop reason */
#pragma once
#include <curses.h>
#include "store.h"

void render_ebpf(WINDOW *win, const mwatch_store_t *store, int *row, int width,
                 int collapsed);

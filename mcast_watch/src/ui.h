/* ui.h – ncurses single-screen UI */
#pragma once

#include "store.h"

/* Initialise ncurses, run the UI loop, tear down on exit. */
void ui_run(mwatch_store_t *store, volatile int *running);

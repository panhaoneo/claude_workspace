/* collector.h – Main 1-second data collection thread */
#pragma once

#include "store.h"

typedef struct {
    mwatch_store_t *store;
    volatile int   *running;
} collector_args_t;

void *collector_thread(void *arg);

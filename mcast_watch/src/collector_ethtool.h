/* collector_ethtool.h – ethtool -S / -g data collection (5s cadence) */
#pragma once

#include "store.h"

/* Populate store->nic ethtool fields.  Called every 5 s by collector. */
void collect_ethtool(mwatch_store_t *store);

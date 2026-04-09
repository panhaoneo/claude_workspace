/* diagnose.h – Diagnostic engine */
#pragma once

#include "store.h"

/*
 * Analyse the current store snapshot and fill store->diag[].
 * Must be called while the store lock is held.
 */
void diagnose(mwatch_store_t *store);

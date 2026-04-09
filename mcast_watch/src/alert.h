/* alert.h – Alert history ring-buffer */
#pragma once

#include "store.h"

void alert_init(void);
void alert_clear(void);
void alert_push(diag_level_t level, const char *msg);
/* Returns number of alerts; fills buf with up to max entries (newest first). */
int  alert_get(diag_item_t *buf, int max);

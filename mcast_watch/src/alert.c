/* alert.c – Alert history ring-buffer */
#include "alert.h"
#include <string.h>
#include <stdio.h>

static diag_item_t s_alerts[MAX_ALERTS];
static int         s_head  = 0;
static int         s_count = 0;

void alert_init(void) {
    memset(s_alerts, 0, sizeof(s_alerts));
    s_head  = 0;
    s_count = 0;
}

void alert_clear(void) {
    alert_init();
}

void alert_push(diag_level_t level, const char *msg) {
    int idx = s_head % MAX_ALERTS;
    s_alerts[idx].level = level;
    snprintf(s_alerts[idx].summary, sizeof(s_alerts[idx].summary), "%s", msg);
    s_head++;
    if (s_count < MAX_ALERTS) s_count++;
}

int alert_get(diag_item_t *buf, int max) {
    int n = (s_count < max) ? s_count : max;
    for (int i = 0; i < n; i++) {
        int idx = ((s_head - 1 - i) % MAX_ALERTS + MAX_ALERTS) % MAX_ALERTS;
        buf[i] = s_alerts[idx];
    }
    return n;
}

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "alert.h"
#include "store.h"

/* Caller must hold g_store.lock */
void alert_push(const char *fmt, ...)
{
    time_t     now = time(NULL);
    struct tm *tm  = localtime(&now);

    /* Shift old alerts down to make room at [0] */
    if (g_store.alert_count < MAX_ALERTS) {
        g_store.alert_count++;
    }
    for (int i = g_store.alert_count - 1; i > 0; i--) {
        g_store.alerts[i] = g_store.alerts[i - 1];
    }

    alert_t *a = &g_store.alerts[0];
    strftime(a->time_str, sizeof(a->time_str), "%H:%M:%S", tm);

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->msg, sizeof(a->msg), fmt, ap);
    va_end(ap);
}

/* Caller must hold g_store.lock */
void alert_evaluate(void)
{
    /* NIC DROP: drop_delta > 0 for 5 consecutive ticks */
    for (int i = 0; i < g_store.nic_count; i++) {
        nic_stat_t *n = &g_store.nics[i];
        if (n->drop_delta > 0) {
            n->drop_streak++;
        } else {
            n->drop_streak = 0;
        }
        if (n->drop_streak == 5) {
            alert_push("%s DROP +%lu sustained 5 ticks",
                       n->name, (unsigned long)n->drop_delta);
        }
    }

    /* CPU softirq > 30% */
    for (int i = 0; i < g_store.cpu_count; i++) {
        if (g_store.cpus[i].softirq_pct > 30.0) {
            alert_push("CPU%d softirq %.1f%% > 30%%",
                       i, g_store.cpus[i].softirq_pct);
        }
    }

    /* Softnet time_squeezed delta > 0 */
    for (int i = 0; i < g_store.cpu_count; i++) {
        if (g_store.softnet[i].squeezed_delta > 0) {
            alert_push("CPU%d softnet time_squeezed +%lu",
                       i, (unsigned long)g_store.softnet[i].squeezed_delta);
        }
    }

    /* IRQ imbalance: single queue > 80% of NIC total */
    for (int i = 0; i < g_store.irq_count; i++) {
        irq_stat_t *irq = &g_store.irqs[i];
        uint64_t total = 0;
        uint64_t maxv  = 0;
        for (int c = 0; c < g_store.cpu_count; c++) {
            total += irq->delta[c];
            if (irq->delta[c] > maxv) maxv = irq->delta[c];
        }
        if (total > 0 && maxv * 100 / total > 80) {
            alert_push("IRQ imbalance: %s single CPU >80%% of interrupts",
                       irq->name);
        }
    }
}

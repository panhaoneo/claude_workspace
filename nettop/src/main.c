#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include "store.h"
#include "collector.h"
#include "ui.h"

static pthread_t g_collector_tid;

static void signal_handler(int sig)
{
    (void)sig;
    collector_stop();
    ui_cleanup();
    store_destroy();
    exit(0);
}

int main(void)
{
    store_init();

    /* Set up signal handlers before anything else */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Start collector thread */
    if (pthread_create(&g_collector_tid, NULL, collector_thread, NULL) != 0) {
        fprintf(stderr, "nettop: failed to create collector thread\n");
        store_destroy();
        return 1;
    }

    /* Run UI on main thread (blocks until 'q' or signal) */
    ui_init();
    ui_run();

    /* Graceful shutdown */
    pthread_join(g_collector_tid, NULL);
    ui_cleanup();
    store_destroy();

    return 0;
}

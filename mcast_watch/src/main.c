/* main.c – mcast_watch v1.1 entry point */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include "store.h"
#include "collector.h"
#include "ui.h"
#include "ebpf_drop.h"
#include "alert.h"

#ifndef VERSION
#define VERSION "1.1"
#endif

static volatile int g_running = 1;
static mwatch_store_t g_store;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static int iface_exists(const char *ifname) {
    char path[64];
    struct stat st;
    snprintf(path, sizeof(path), "/sys/class/net/%s", ifname);
    return (stat(path, &st) == 0);
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s -i <interface> [options]\n"
        "\n"
        "Required:\n"
        "  -i <interface>    Network interface to monitor (e.g. eth1)\n"
        "\n"
        "Options:\n"
        "  --no-ebpf         Disable eBPF, use pure /proc mode\n"
        "  -h                Show this help\n"
        "  -v                Show version\n"
        "\n"
        "Keyboard controls:\n"
        "  q / Ctrl+C        Quit\n"
        "  r                 Manual refresh\n"
        "  c                 Clear alerts and eBPF counters\n"
        "  e                 Toggle eBPF section collapse\n",
        prog);
}

int main(int argc, char *argv[]) {
    char ifname[IF_NAME_LEN] = {0};
    int  no_ebpf = 0;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-i") == 0) && (i + 1 < argc)) {
            strncpy(ifname, argv[++i], IF_NAME_LEN - 1);
        } else if (strcmp(argv[i], "--no-ebpf") == 0) {
            no_ebpf = 1;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 ||
                   strcmp(argv[i], "--version") == 0) {
            printf("mcast_watch v%s\n", VERSION);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (ifname[0] == '\0') {
        fprintf(stderr, "Error: -i <interface> is required\n\n");
        usage(argv[0]);
        return 1;
    }

    if (!iface_exists(ifname)) {
        fprintf(stderr, "Error: interface '%s' does not exist\n", ifname);
        return 1;
    }

    /* Initialise shared store */
    memset(&g_store, 0, sizeof(g_store));
    snprintf(g_store.ifname,     IF_NAME_LEN, "%.*s", IF_NAME_LEN - 1, ifname);
    snprintf(g_store.nic.ifname, IF_NAME_LEN, "%.*s", IF_NAME_LEN - 1, ifname);
    pthread_mutex_init(&g_store.lock, NULL);

    alert_init();

    /* Signal handling */
    struct sigaction sa = {0};
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* eBPF initialisation (graceful degradation on failure) */
    if (!no_ebpf) {
        ebpf_init(&g_store);
        /* ebpf_init sets store->ebpf.enabled and prints a message on failure */
    } else {
        g_store.ebpf.enabled = 0;
    }

    /* Start collector thread */
    pthread_t collector_tid;
    collector_args_t cargs = { .store = &g_store, .running = &g_running };
    if (pthread_create(&collector_tid, NULL, collector_thread, &cargs) != 0) {
        perror("pthread_create(collector)");
        ebpf_cleanup(&g_store);
        return 1;
    }

    /* Run UI in main thread (blocks until quit) */
    ui_run(&g_store, &g_running);

    /* Shutdown */
    g_running = 0;
    pthread_join(collector_tid, NULL);
    ebpf_cleanup(&g_store);
    pthread_mutex_destroy(&g_store.lock);

    return 0;
}

/* ebpf_drop.c – eBPF kfree_skb drop-reason loader & ring-buffer consumer */
#include "ebpf_drop.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>

#ifndef NO_EBPF
/* ── eBPF-enabled build ── */
#include <bpf/libbpf.h>
#include "kfree_skb.skel.h"

/* Shared event struct (must match kfree_skb.bpf.c) */
struct drop_event {
    uint32_t reason;
    uint32_t pad;
};

/* Known drop reason strings */
static const struct { uint32_t id; const char *str; } reason_map[] = {
    {  2, "NOT_SPECIFIED"        },
    {  3, "NO_SOCKET"            },
    {  4, "PKT_TOO_SMALL"        },
    {  5, "TCP_CSUM"             },
    {  6, "SOCKET_FILTER"        },
    {  7, "UDP_CSUM"             },
    {  8, "NETFILTER_DROP"       },
    {  9, "OTHERHOST"            },
    { 10, "IP_CSUM"              },
    { 11, "IP_INHDR"             },
    { 12, "IP_RPFILTER"          },
    { 13, "UNICAST_IN_L2_MULTICAST" },
    { 14, "XFRM_POLICY"         },
    { 15, "IP_NOPROTO"           },
    { 16, "SOCKET_RCVBUFF"       },
    { 17, "PROTO_MEM"            },
    { 18, "TCP_MD5NOTFOUND"      },
    { 19, "TCP_MD5UNEXPECTED"    },
    { 20, "TCP_MD5FAILURE"       },
    { 21, "SOCKET_BACKLOG"       },
    { 22, "TCP_FLAGS"            },
    { 23, "TCP_ZEROWINDOW"       },
    { 24, "TCP_OLD_DATA"         },
    { 25, "TCP_OVERWINDOW"       },
    { 26, "TCP_OFOMERGE"         },
    { 27, "TCP_RFC7323_PAWS"     },
    { 28, "TCP_INVALID_SEQUENCE" },
    { 29, "TCP_RESET"            },
    { 30, "TCP_INVALID_SYN"      },
    { 31, "TCP_CLOSE"            },
    { 32, "TCP_FASTOPEN"         },
    { 33, "TCP_MINTTL"           },
    { 34, "TCP_INVALID_ACK_SEQUENCE" },
    { 35, "TCP_TOO_OLD_ACK"     },
    { 36, "TCP_ACK_UNSENT_DATA" },
    { 37, "TCP_OFO_QUEUE_PRUNE" },
    { 38, "TCP_OFO_DROP"        },
    { 39, "TCP_READ_QUEUE_FULL" },
    { 40, "TCP_TOO_MANY_ORPHANS"},
    { 41, "TCP_ABORTed_ON_DATA" },
    { 42, "TCP_ABORTed_ON_CLOSE"},
    { 43, "TCP_ABORTed_ON_MEMORY"},
    { 44, "TCP_ABORTed_ON_TIMEOUT"},
    { 45, "TCP_ABORTed_ON_LINGER"},
    { 46, "TCP_RECV_ERROR"      },
    { 47, "TCP_COLLAPSE_FAIL"   },
    { 48, "TCP_SPURIOUS_RETRAN" },
    { 49, "ICMP_CSUM"           },
    { 50, "INVALID_PROTO"       },
    { 51, "IP_INADDRERRORS"     },
    { 52, "IP_INNOROUTES"       },
    { 53, "PKT_TOO_BIG"         },
    { 54, "DUP_FRAG"            },
    { 55, "FRAG_REASM_TIMEOUT"  },
    { 56, "FRAG_TOO_FAR"        },
    { 57, "TCP_MINMSS"          },
    { 58, "IPV6DISABLED"        },
    { 59, "NEIGH_CREATEFAIL"    },
    { 60, "NEIGH_FAILED"        },
    { 61, "NEIGH_QUEUEFULL"     },
    { 62, "NEIGH_DEAD"          },
    { 63, "TC_EGRESS"           },
    { 64, "QDISC_DROP"          },
    { 65, "CPU_BACKLOG"         },
    { 66, "XDP"                 },
    { 67, "TC_INGRESS"          },
    { 68, "UNHANDLED_PROTO"     },
    { 69, "SKB_CSUM"            },
    { 70, "SKB_GSO_SEG"         },
    { 71, "SKB_UCOPY_FAULT"     },
    { 72, "DEV_HDR"             },
    { 73, "DEV_READY"           },
    { 74, "FULL_RING"           },
    { 75, "NOMEM"               },
    { 76, "HDR_TRUNC"           },
    { 77, "TAP_FILTER"          },
    { 78, "TAP_TXFILTER"        },
    { 79, "ICMP_NOPOLICY"       },
    { 80, "UDP_SOCK_FULL"       },
    { 81, "UDP_NO_SKB_FRAG"     },
};
#define REASON_MAP_SIZE ((int)(sizeof(reason_map)/sizeof(reason_map[0])))

static const char *reason_to_str(uint32_t reason) {
    for (int i = 0; i < REASON_MAP_SIZE; i++)
        if (reason_map[i].id == reason) return reason_map[i].str;
    return "UNKNOWN";
}

/* Thread state */
typedef struct {
    struct kfree_skb_bpf *skel;
    struct ring_buffer   *rb;
    mwatch_store_t       *store;
    pthread_t             tid;
    volatile int          running;
} ebpf_ctx_t;

static ebpf_ctx_t g_ebpf_ctx;

/* Per-reason accumulator (reset each second by collector) */
static uint64_t s_prev_count[MAX_DROP_REASONS];

/* ring-buffer callback – called from ebpf_thread under poll */
static int handle_drop_event(void *ctx, void *data, size_t data_sz) {
    (void)ctx; (void)data_sz;
    const struct drop_event *e = (const struct drop_event *)data;
    mwatch_store_t *store = g_ebpf_ctx.store;

    pthread_mutex_lock(&store->lock);
    ebpf_stat_t *es = &store->ebpf;

    /* Find or create entry for this reason */
    int idx = -1;
    for (int i = 0; i < es->reason_count; i++) {
        if (es->reasons[i].reason == e->reason) { idx = i; break; }
    }
    if (idx < 0 && es->reason_count < MAX_DROP_REASONS) {
        idx = es->reason_count++;
        es->reasons[idx].reason = e->reason;
        snprintf(es->reasons[idx].reason_str, sizeof(es->reasons[idx].reason_str),
                 "%s", reason_to_str(e->reason));
        es->reasons[idx].count  = 0;
        es->reasons[idx].count_s = 0;
    }
    if (idx >= 0) {
        es->reasons[idx].count++;
        es->reasons[idx].count_s++;   /* reset to 0 each second by collector */
    }
    pthread_mutex_unlock(&store->lock);
    return 0;
}

/* eBPF poll thread */
static void *ebpf_thread(void *arg) {
    (void)arg;
    while (g_ebpf_ctx.running) {
        ring_buffer__poll(g_ebpf_ctx.rb, 100 /* ms timeout */);

        /* Reset per-second counters every ~1s */
        static struct timespec last = {0, 0};
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (last.tv_sec == 0) { last = now; continue; }
        if (now.tv_sec > last.tv_sec) {
            pthread_mutex_lock(g_ebpf_ctx.store ? &g_ebpf_ctx.store->lock : NULL);
            ebpf_stat_t *es = &g_ebpf_ctx.store->ebpf;
            for (int i = 0; i < es->reason_count; i++)
                es->reasons[i].count_s = 0;
            pthread_mutex_unlock(&g_ebpf_ctx.store->lock);
            last = now;
        }
    }
    return NULL;
}

int ebpf_init(mwatch_store_t *store) {
    memset(&g_ebpf_ctx, 0, sizeof(g_ebpf_ctx));
    g_ebpf_ctx.store = store;

    /* Check BTF */
    struct stat st;
    if (stat("/sys/kernel/btf/vmlinux", &st) != 0) {
        fprintf(stderr, "[eBPF] /sys/kernel/btf/vmlinux not found – disabled\n");
        store->ebpf.enabled = 0;
        return -1;
    }
    /* Check root */
    if (geteuid() != 0) {
        fprintf(stderr, "[eBPF] not root – disabled\n");
        store->ebpf.enabled = 0;
        return -1;
    }

    /* Suppress libbpf messages unless debug needed */
    libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

    struct kfree_skb_bpf *skel = kfree_skb_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "[eBPF] failed to load BPF program – disabled\n");
        store->ebpf.enabled = 0;
        return -1;
    }

    if (kfree_skb_bpf__attach(skel) != 0) {
        fprintf(stderr, "[eBPF] failed to attach – disabled\n");
        kfree_skb_bpf__destroy(skel);
        store->ebpf.enabled = 0;
        return -1;
    }

    struct ring_buffer *rb = ring_buffer__new(
        bpf_map__fd(skel->maps.rb), handle_drop_event, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "[eBPF] failed to create ring buffer – disabled\n");
        kfree_skb_bpf__destroy(skel);
        store->ebpf.enabled = 0;
        return -1;
    }

    g_ebpf_ctx.skel    = skel;
    g_ebpf_ctx.rb      = rb;
    g_ebpf_ctx.running = 1;
    store->ebpf.enabled = 1;

    pthread_create(&g_ebpf_ctx.tid, NULL, ebpf_thread, NULL);
    return 0;
}

void ebpf_cleanup(mwatch_store_t *store) {
    (void)store;
    g_ebpf_ctx.running = 0;
    if (g_ebpf_ctx.tid)
        pthread_join(g_ebpf_ctx.tid, NULL);
    if (g_ebpf_ctx.rb)
        ring_buffer__free(g_ebpf_ctx.rb);
    if (g_ebpf_ctx.skel)
        kfree_skb_bpf__destroy(g_ebpf_ctx.skel);
}

#else  /* NO_EBPF */
/* ── Stub implementations for NO_EBPF build ── */

int ebpf_init(mwatch_store_t *store) {
    store->ebpf.enabled = 0;
    return -1;
}

void ebpf_cleanup(mwatch_store_t *store) {
    (void)store;
}
#endif /* NO_EBPF */

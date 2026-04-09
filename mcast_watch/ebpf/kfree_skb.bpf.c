/* kfree_skb.bpf.c – eBPF CO-RE program: tracepoint/skb/kfree_skb
 *
 * Tracks every skb freed with a non-zero drop reason and sends an event
 * to user-space via a ring buffer.
 *
 * Targets: kernel 5.15+ with BTF enabled (CO-RE).
 * The `reason` field was introduced in the kfree_skb tracepoint in
 * kernel 5.17; on earlier kernels bpf_core_field_exists() returns 0
 * and we report SKB_DROP_REASON_NOT_SPECIFIED instead.
 *
 * Build:
 *   clang -target bpf -O2 -g -c kfree_skb.bpf.c -o kfree_skb.bpf.o
 *   bpftool gen skeleton kfree_skb.bpf.o > ../src/kfree_skb.skel.h
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

/* SKB_NOT_DROPPED_YET == 0; filter these out */
#define SKB_NOT_DROPPED_YET  0
#define SKB_DROP_REASON_NOT_SPECIFIED 2

/* Event sent to user-space */
struct drop_event {
    __u32 reason;
    __u32 pad;
};

/* Ring buffer map (256 KB) */
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} rb SEC(".maps");

/*
 * CO-RE accessor for the `reason` field.
 * If the field doesn't exist in the running kernel (pre-5.17 without
 * backport), bpf_core_field_exists returns 0 and we use the fallback.
 */
SEC("tracepoint/skb/kfree_skb")
int handle_kfree_skb(struct trace_event_raw_kfree_skb *ctx)
{
    __u32 reason;

    if (bpf_core_field_exists(ctx->reason)) {
        reason = (__u32)BPF_CORE_READ(ctx, reason);
    } else {
        /* Kernel doesn't have reason field; treat every kfree_skb
         * as NOT_SPECIFIED so it still appears in the stats. */
        reason = SKB_DROP_REASON_NOT_SPECIFIED;
    }

    /* Skip non-drop events */
    if (reason <= SKB_NOT_DROPPED_YET)
        return 0;

    struct drop_event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;

    e->reason = reason;
    e->pad    = 0;
    bpf_ringbuf_submit(e, 0);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";

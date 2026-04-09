/* ebpf_drop.h – eBPF kfree_skb drop-reason tracker */
#pragma once

#include "store.h"

/*
 * Try to load the BPF program.  On success, starts a background thread
 * that polls the ring-buffer and accumulates per-reason counters in
 * store->ebpf.  On any failure (no root, no BTF, compile error, …)
 * sets store->ebpf.enabled = 0 and returns -1; all other store fields
 * remain valid so the tool continues in /proc-only mode.
 */
int  ebpf_init(mwatch_store_t *store);

/* Unload BPF program and stop the poll thread (called at exit). */
void ebpf_cleanup(mwatch_store_t *store);

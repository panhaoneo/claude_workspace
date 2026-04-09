/* collector_ss.h – ss -u -i -n -p per-socket UDP drop collection */
#pragma once

#include "store.h"

/*
 * Run "ss -u -i -n -p", parse the output and write results into
 * a local buffer.  The caller (collector) copies it under the store lock.
 */
int collect_ss(socket_stat_t *out, int max_sockets);

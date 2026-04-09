/* collector_ss.c – ss -u -i -n -p per-socket UDP drop collection */
#include "collector_ss.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/*
 * Parse output like:
 *   UNCONN 0  0  0.0.0.0:7700  *
 *            skmem:(r0,rb212992,t0,tb212992,...) drops:156
 *            users:(("md_gateway",pid=1234,fd=8))
 *
 * We look for the "drops:" field in the info line that follows each
 * socket line.
 */
int collect_ss(socket_stat_t *out, int max_sockets) {
    FILE *fp = popen("ss -u -i -n -p 2>/dev/null", "r");
    if (!fp) return 0;

    int count = 0;
    char line[512];
    socket_stat_t cur;
    memset(&cur, 0, sizeof(cur));
    int have_cur = 0;

    while (fgets(line, sizeof(line), fp) && count < max_sockets) {
        /* Socket state line: starts with UNCONN or ESTAB etc. */
        if (line[0] != ' ' && line[0] != '\t') {
            /* Save previous socket if it had drops */
            if (have_cur) {
                out[count++] = cur;
            }
            memset(&cur, 0, sizeof(cur));
            have_cur = 0;

            /* Parse: State Recv-Q Send-Q Local Peer */
            char state[16], local[48], peer[48];
            unsigned long rq, sq;
            if (sscanf(line, "%15s %lu %lu %47s %47s",
                       state, &rq, &sq, local, peer) >= 4) {
                /* Skip header line */
                if (strcmp(state, "State") == 0) continue;
                snprintf(cur.local_addr, sizeof(cur.local_addr), "%s", local);
                have_cur = 1;
            }
        } else {
            /* Info continuation line */
            if (!have_cur) continue;

            /* Extract drops */
            char *p = strstr(line, "drops:");
            if (p) {
                unsigned long long d;
                if (sscanf(p + 6, "%llu", &d) == 1)
                    cur.drops = (uint64_t)d;
            }

            /* Extract rb (recv buf) from skmem */
            p = strstr(line, "rb");
            if (p && p > line && *(p-1) == ',') {
                unsigned long rb;
                if (sscanf(p + 2, "%lu", &rb) == 1)
                    cur.rcvbuf = (uint32_t)rb;
            } else {
                /* Also try "(r0,rbNNN," format */
                p = strstr(line, ",rb");
                if (p) {
                    unsigned long rb;
                    if (sscanf(p + 3, "%lu", &rb) == 1)
                        cur.rcvbuf = (uint32_t)rb;
                }
            }

            /* Extract process name and pid from users:((...)) */
            p = strstr(line, "users:((");
            if (p) {
                char pname[PROC_NAME_LEN];
                int pid;
                /* Format: users:(("name",pid=N,fd=M)) */
                if (sscanf(p + 8, "(\"%31[^\"]\",pid=%d", pname, &pid) == 2) {
                    snprintf(cur.proc_name, sizeof(cur.proc_name), "%s", pname);
                    cur.pid = (pid_t)pid;
                }
            }
        }
    }
    /* Don't forget the last socket */
    if (have_cur && count < max_sockets) {
        out[count++] = cur;
    }

    pclose(fp);
    return count;
}

/*
 * fast_stat.c - Latency Toolkit v3 High-Performance Stat Engine
 *
 * Single-pass O(N) processor: parses raw feed log lines and accumulates a
 * delay histogram.  Emits partial stats in the merge_stats.awk format so
 * results from multiple parallel workers can be merged.
 *
 * Compile:
 *   cc -O2 -o fast_stat fast_stat.c
 *
 * Usage:
 *   ./fast_stat <logfile>
 *   ./fast_stat <logfile> <chunk_start_line> <chunk_end_line>
 *
 * Line format (fixed-width, ASCII):
 *   Pos  0-7  : YYYYMMDD  (local date)
 *   Pos  8-13 : HHMMSS    (local time, 6 chars)
 *   Pos 14    : '.'
 *   Pos 15-20 : sssuuu    (local fractional microseconds, 6 digits)
 *   Pos 21    : ' '
 *   Pos 22-29 : YYYYMMDD  (exchange date)
 *   Pos 30-35 : HHMMSS    (exchange time, 6 chars)
 *   Pos 36-38 : ms        (exchange milliseconds, 3 digits)
 *   Pos 39    : '\n'
 *
 * Output:
 *   COUNT <n>
 *   SUM   <total_us>
 *   MIN   <min_us>
 *   MAX   <max_us>
 *   B <bucket_idx> <count>
 *   ...
 *
 * Histogram config:
 *   BUCKET_US = 1000   (1 ms per bucket)
 *   OFFSET    = 10000  (handles negative delays down to -10 s)
 *   MAX_IDX   = 70000  (covers up to +60 s)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUCKET_US  1000L
#define OFFSET     10000
#define MAX_IDX    70000
#define BUF_SIZE   (1 << 20)    /* 1 MB read buffer */

static long hist[MAX_IDX];

/* Convert 6-char HHMMSS to seconds of day */
static inline long hhmmss_to_sec(const char *s) {
    return (long)((s[0]-'0')*10 + (s[1]-'0')) * 3600
         + (long)((s[2]-'0')*10 + (s[3]-'0')) * 60
         + (long)((s[4]-'0')*10 + (s[5]-'0'));
}

/* Convert n ASCII digits to long */
static inline long digits_to_long(const char *s, int n) {
    long v = 0;
    for (int i = 0; i < n; i++) v = v * 10 + (s[i] - '0');
    return v;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: fast_stat <logfile> [start_line end_line]\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }

    /* Optional line-range for parallel chunk processing */
    long range_start = 0;
    long range_end   = (long)9e18;
    if (argc >= 4) {
        range_start = atol(argv[2]);   /* inclusive, 1-based */
        range_end   = atol(argv[3]);   /* inclusive */
    }

    char *buf = malloc(BUF_SIZE);
    if (!buf) { perror("malloc"); fclose(f); return 1; }

    long count   = 0;
    long total   = 0;
    long min_val =  (long)9e15;
    long max_val = -(long)9e15;
    long lineno  = 0;

    /* Streaming line reader using a large buffer */
    size_t bytes_in = 0, pos = 0;
    char line[128];
    int  lpos = 0;
    int  done = 0;

    while (!done) {
        /* Refill buffer */
        if (pos >= bytes_in) {
            bytes_in = fread(buf, 1, BUF_SIZE, f);
            pos = 0;
            if (bytes_in == 0) { done = 1; break; }
        }

        /* Scan for newline */
        char c = buf[pos++];
        if (c == '\n' || c == '\r') {
            if (lpos == 0) continue;   /* skip empty lines */
            line[lpos] = '\0';
            lpos = 0;
            lineno++;

            if (lineno < range_start) continue;
            if (lineno > range_end)   { done = 1; break; }

            if (lpos == 0 && strlen(line) < 38) goto next_line;

            int len = (int)strlen(line);
            if (len < 38) goto next_line;

            long sl = hhmmss_to_sec(line +  8);
            long fl = digits_to_long(line + 15, 6);
            long se = hhmmss_to_sec(line + 30);
            long fe = digits_to_long(line + 36, 3);

            long d = (sl - se) * 1000000L + fl - fe * 1000L;

            count++;
            total += d;
            if (d < min_val) min_val = d;
            if (d > max_val) max_val = d;

            long b = d / BUCKET_US + OFFSET;
            if (b < 0)        b = 0;
            if (b >= MAX_IDX) b = MAX_IDX - 1;
            hist[b]++;

            next_line: ;
        } else {
            if (lpos < (int)sizeof(line) - 1)
                line[lpos++] = c;
        }
    }

    free(buf);
    fclose(f);

    if (count == 0) return 0;

    printf("COUNT %ld\n", count);
    printf("SUM   %ld\n", total);
    printf("MIN   %ld\n", min_val);
    printf("MAX   %ld\n", max_val);

    for (int b = 0; b < MAX_IDX; b++)
        if (hist[b])
            printf("B %d %ld\n", b, hist[b]);

    return 0;
}

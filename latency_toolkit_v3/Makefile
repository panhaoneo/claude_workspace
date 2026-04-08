# Latency Toolkit v3 - Build
#
# Builds the optional C fast_stat binary for high-performance log analysis.
# The toolkit works without this binary (falls back to awk), but building it
# enables processing of 100M+ line logs within the 10-second target.
#
# Usage:
#   make          - build fast_stat
#   make clean    - remove binary
#   make test     - run a quick smoke test

CC      ?= cc
CFLAGS  ?= -O2 -Wall
TARGET   = core/fast_stat

.PHONY: all clean test

all: $(TARGET)

$(TARGET): core/fast_stat.c
	$(CC) $(CFLAGS) -o $@ $<
	@echo "Built: $@"

clean:
	rm -f $(TARGET)

test: $(TARGET)
	@echo "=== Generating 100K-line test log ==="
	@awk 'BEGIN{for(i=0;i<100000;i++){d=int(i%20)*1000+500;h=9;m=int(i/600000);s=int(i%600000/10000);e_hms=sprintf("%02d%02d%02d",h,m,s);l_frac=sprintf("%06d",d);printf "20260304%s.%s 20260304%s000\n",e_hms,l_frac,e_hms}}' > /tmp/lt_test.log
	@echo "=== Running fast_stat ==="
	@$(TARGET) /tmp/lt_test.log | awk -f stat/merge_stats.awk
	@echo "=== Test passed ==="
	@rm -f /tmp/lt_test.log

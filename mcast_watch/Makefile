# Makefile for mcast_watch v1.1
#
# Usage:
#   make          – build with eBPF support (requires clang, bpftool, libbpf)
#   make noBPF    – build without eBPF (pure /proc mode, no extra deps)
#   make clean    – remove build artefacts
#
# Notes:
#   libbpf is expected to be available as a pkg-config package.
#   On OpenEuler 22.03 SP3: dnf install libbpf-devel clang bpftool
#
# ── Toolchain ─────────────────────────────────────────────────────────────

CC      = gcc
CLANG   = clang
BPFTOOL = bpftool

# ── Directories ───────────────────────────────────────────────────────────

SRCDIR  = src
EBPFDIR = ebpf

# ── Version ───────────────────────────────────────────────────────────────

VERSION = 1.1
TARGET  = mcast_watch

# ── Compiler flags ────────────────────────────────────────────────────────

CFLAGS  = -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE \
          -DVERSION=\"$(VERSION)\" \
          -I$(SRCDIR)

LDFLAGS = -lncurses -lpthread

# BPF object compilation flags
BPF_CFLAGS = -target bpf -O2 -g

# ── Source files ──────────────────────────────────────────────────────────

SRCS = $(SRCDIR)/main.c          \
       $(SRCDIR)/collector.c     \
       $(SRCDIR)/collector_ethtool.c \
       $(SRCDIR)/collector_ss.c  \
       $(SRCDIR)/diagnose.c      \
       $(SRCDIR)/ui.c            \
       $(SRCDIR)/render_nic.c    \
       $(SRCDIR)/render_kernel.c \
       $(SRCDIR)/render_cpu.c    \
       $(SRCDIR)/render_socket.c \
       $(SRCDIR)/render_ebpf.c   \
       $(SRCDIR)/render_diag.c   \
       $(SRCDIR)/ebpf_drop.c     \
       $(SRCDIR)/alert.c

# ── BPF skeleton (generated) ──────────────────────────────────────────────

BPF_SRC  = $(EBPFDIR)/kfree_skb.bpf.c
BPF_OBJ  = $(EBPFDIR)/kfree_skb.bpf.o
SKEL_HDR = $(SRCDIR)/kfree_skb.skel.h

# ── Default: build WITH eBPF ──────────────────────────────────────────────

.PHONY: all noBPF clean vmlinux_h

all: $(SKEL_HDR) $(TARGET)

# Generate vmlinux.h from the running kernel (requires bpftool + BTF)
vmlinux_h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $(EBPFDIR)/vmlinux.h
	@echo "Generated $(EBPFDIR)/vmlinux.h"

# Compile BPF kernel-side program
$(BPF_OBJ): $(BPF_SRC) $(EBPFDIR)/vmlinux.h
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

# Generate C skeleton header from BPF object
$(SKEL_HDR): $(BPF_OBJ)
	$(BPFTOOL) gen skeleton $< > $@

# Link full binary with eBPF support
$(TARGET): $(SRCS) $(SKEL_HDR)
	$(CC) $(CFLAGS) -DHAVE_EBPF $(SRCS) -o $(TARGET) $(LDFLAGS) \
	    $$(pkg-config --libs libbpf 2>/dev/null || echo "-lbpf -lelf -lz")

# ── NO-eBPF build ─────────────────────────────────────────────────────────

noBPF:
	$(CC) $(CFLAGS) -DNO_EBPF $(SRCS) -o $(TARGET) $(LDFLAGS)
	@echo "Built $(TARGET) without eBPF support"

# ── Clean ─────────────────────────────────────────────────────────────────

clean:
	rm -f $(TARGET)
	rm -f $(BPF_OBJ)
	rm -f $(SKEL_HDR)
	rm -f $(EBPFDIR)/vmlinux.h

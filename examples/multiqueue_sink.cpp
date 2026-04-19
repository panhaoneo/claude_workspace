// Multi-queue receive example using ViSet.
#include "efvi/vi_set.hpp"
#include "efvi/config.hpp"
#include <cstdio>
#include <csignal>
#include <thread>
#include <vector>

static volatile bool g_running = true;
static void handle_signal(int) { g_running = false; }

static void rx_thread(efvi::Vi& vi, int queue_idx) {
    efvi::RxBatch batch;
    while (g_running) {
        vi.poll(batch);
        for (const auto& pkt : batch) {
            (void)pkt.data();
            (void)pkt.len();
        }
    }
    auto s = vi.stats_snapshot();
    fprintf(stderr, "queue[%d] rx=%llu drops=%llu\n",
            queue_idx,
            (unsigned long long)s.rx_packets,
            (unsigned long long)s.rx_drops);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: multiqueue_sink <interface> <num_queues>\n");
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int nq = atoi(argv[2]);

    efvi::ViSetConfig cfg;
    cfg.queue_count       = nq;
    cfg.base_cfg.interface = argv[1];

    efvi::ViSet vs(cfg);
    fprintf(stderr, "Starting %d queues on %s\n", vs.queue_count(), argv[1]);

    std::vector<std::thread> threads;
    for (int i = 0; i < vs.queue_count(); ++i)
        threads.emplace_back(rx_thread, std::ref(vs.vi(i)), i);

    for (auto& t : threads) t.join();

    auto agg = vs.aggregate_stats();
    fprintf(stderr, "total rx=%llu drops=%llu\n",
            (unsigned long long)agg.rx_packets,
            (unsigned long long)agg.rx_drops);
    return 0;
}

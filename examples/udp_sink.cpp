// Single-queue UDP receive example.
// Build with real NIC: cmake -DEFVI_BUILD_EXAMPLES=ON -DONLOAD_SRC_DIR=...
#include "efvi/vi.hpp"
#include "efvi/config.hpp"
#include <cstdio>
#include <csignal>

static volatile bool g_running = true;
static void handle_signal(int) { g_running = false; }

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: udp_sink <interface> <local_port>\n");
        return 1;
    }

    const char* iface = argv[1];
    int port = atoi(argv[2]);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    efvi::ViConfig cfg;
    cfg.interface = iface;
    cfg.log_callback = [](efvi::LogLevel lvl, const std::string& msg) {
        const char* ls = (lvl == efvi::LogLevel::ERROR ? "ERROR" :
                          lvl == efvi::LogLevel::WARN  ? "WARN"  :
                          lvl == efvi::LogLevel::INFO  ? "INFO"  : "DEBUG");
        fprintf(stderr, "[%s] %s\n", ls, msg.c_str());
    };
    cfg.filters.push_back(efvi::FilterSpec::udp(0, static_cast<uint16_t>(port)));

    efvi::Vi vi(cfg);
    fprintf(stderr, "Listening on %s port %d (arch=%s)\n", iface, port,
            vi.arch() == efvi::NicArch::EF10 ? "EF10" : "EfCT");

    efvi::RxBatch batch;
    uint64_t total_pkts = 0;

    while (g_running) {
        int n = vi.poll(batch);
        for (int i = 0; i < n; ++i) {
            ++total_pkts;
            (void)batch[i].data();
        }
    }

    auto s = vi.stats_snapshot();
    fprintf(stderr, "rx=%llu drops=%llu\n",
            (unsigned long long)s.rx_packets,
            (unsigned long long)s.rx_drops);
    return 0;
}

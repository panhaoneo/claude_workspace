// Single-queue UDP send example.
#include "efvi/vi.hpp"
#include "efvi/config.hpp"
#include <cstdio>
#include <cstring>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: udp_send <interface>\n");
        return 1;
    }

    efvi::ViConfig cfg;
    cfg.interface = argv[1];
    cfg.log_callback = [](efvi::LogLevel, const std::string& msg) {
        fprintf(stderr, "%s\n", msg.c_str());
    };

    efvi::Vi vi(cfg);

    // Pre-built Ethernet frame (dummy payload)
    char frame[64];
    memset(frame, 0xAB, sizeof(frame));

    for (int i = 0; i < 1000; ++i)
        vi.send(frame, sizeof(frame));

    auto s = vi.stats_snapshot();
    fprintf(stderr, "sent %llu packets\n", (unsigned long long)s.tx_packets);
    return 0;
}

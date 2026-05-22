// src/main.cpp
//
// VEDA_RTSP — entry point.
//
// Usage:
//   ./veda_rtsp [--port 8554] [--source <h264_file>]

#include "rtsp/server.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

struct CliArgs {
    uint16_t    port   = 8554;
    std::string source = "tools/samples/test.h264";
};

CliArgs parse_args(int argc, char** argv) {
    CliArgs a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        if (k == "--port" && i + 1 < argc) {
            a.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (k == "--source" && i + 1 < argc) {
            a.source = argv[++i];
        }
    }
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    const CliArgs args = parse_args(argc, argv);

    veda::rtsp::Server server(args.port, args.source);
    std::printf("[veda_rtsp] listening on rtsp://0.0.0.0:%u/stream\n", args.port);

    return server.run();
}

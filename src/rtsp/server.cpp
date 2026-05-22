// src/rtsp/server.cpp

#include "rtsp/server.hpp"

#include <utility>

namespace veda::rtsp {

Server::Server(uint16_t port, std::string source_path)
    : port_(port), source_path_(std::move(source_path)) {}

Server::~Server() = default;

int Server::run() {
    // TODO (Week 1):
    //   1. Open listening TCP socket on port_
    //   2. accept() loop -> spawn Session per client
    //   3. Handle SIGINT for graceful shutdown
    return 0;
}

}  // namespace veda::rtsp

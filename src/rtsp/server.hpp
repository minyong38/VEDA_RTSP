// src/rtsp/server.hpp
//
// Top-level RTSP server. Owns the listening TCP socket and dispatches
// incoming connections to Session objects.

#pragma once

#include <cstdint>
#include <string>

namespace veda::rtsp {

class Server {
public:
    Server(uint16_t port, std::string source_path);
    ~Server();

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    // Blocks until the server is stopped (SIGINT) or fatal error.
    // Returns process exit code.
    int run();

private:
    uint16_t    port_;
    std::string source_path_;
};

}  // namespace veda::rtsp

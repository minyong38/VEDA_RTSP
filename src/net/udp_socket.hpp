// src/net/udp_socket.hpp
//
// UDP socket for RTP data delivery (and later, RTCP).

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace veda::net {

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();

    UdpSocket(const UdpSocket&)            = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Bind locally (0 = ephemeral).
    bool bind(uint16_t port);

    // Set remote endpoint for subsequent send_to() calls.
    bool connect_to(const std::string& host, uint16_t port);

    ssize_t send(const void* buf, std::size_t len);

    int fd() const { return fd_; }

private:
    int fd_ = -1;
};

}  // namespace veda::net

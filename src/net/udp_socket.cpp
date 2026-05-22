// src/net/udp_socket.cpp

#include "net/udp_socket.hpp"

#include <unistd.h>

namespace veda::net {

UdpSocket::UdpSocket() {
    // TODO (Week 3): socket(AF_INET, SOCK_DGRAM, 0)
}

UdpSocket::~UdpSocket() {
    if (fd_ >= 0) ::close(fd_);
}

bool UdpSocket::bind(uint16_t /*port*/) {
    // TODO: bind()
    return false;
}

bool UdpSocket::connect_to(const std::string& /*host*/, uint16_t /*port*/) {
    // TODO: connect() so we can use send() instead of sendto()
    return false;
}

ssize_t UdpSocket::send(const void* /*buf*/, std::size_t /*len*/) {
    // TODO: ::send()
    return -1;
}

}  // namespace veda::net

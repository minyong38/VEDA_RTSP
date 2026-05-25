// src/net/udp_socket.cpp

#include "net/udp_socket.hpp"

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <iostream>

namespace veda::net {

UdpSocket::UdpSocket() {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        std::cerr << "[UdpSocket] Failed to create socket\n";
    }
}

UdpSocket::~UdpSocket() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool UdpSocket::bind(uint16_t port) {
    if (fd_ < 0) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[UdpSocket] Failed to bind to port " << port << "\n";
        return false;
    }

    return true;
}

bool UdpSocket::connect_to(const std::string& host, uint16_t port) {
    if (fd_ < 0) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // 호스트 주소 변환
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "[UdpSocket] Invalid address: " << host << "\n";
        return false;
    }

    // connect()를 사용하면 이후 send()만으로 전송 가능 (sendto 대신)
    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[UdpSocket] Failed to connect to " << host << ":" << port << "\n";
        return false;
    }

    return true;
}

ssize_t UdpSocket::send(const void* buf, std::size_t len) {
    if (fd_ < 0 || buf == nullptr) return -1;

    return ::send(fd_, buf, len, 0);
}

}  // namespace veda::net

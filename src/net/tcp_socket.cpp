// src/net/tcp_socket.cpp

#include "tcp_socket.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>

namespace veda::net {

TcpListener::TcpListener(uint16_t port) {
    // 1. 소켓 생성
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << "\n";
        return;
    }

    // 2. SO_REUSEADDR 설정 (재시작 시 "Address already in use" 방지)
    int opt = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt() failed: " << std::strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return;
    }

    // 3. bind() - 0.0.0.0:port에 바인드
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;  // 모든 인터페이스
    addr.sin_port = htons(port);

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed: " << std::strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return;
    }

    // 4. listen() - 연결 대기 시작
    if (::listen(fd_, 5) < 0) {
        std::cerr << "listen() failed: " << std::strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return;
    }

    std::cout << "[TCP] Listening on port " << port << "\n";
}

TcpListener::~TcpListener() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

int TcpListener::accept_one() {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    int client_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (client_fd < 0) {
        std::cerr << "accept() failed: " << std::strerror(errno) << "\n";
        return -1;
    }

    // 클라이언트 IP 출력
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
    std::cout << "[TCP] Client connected: " << ip_str
              << ":" << ntohs(client_addr.sin_port) << "\n";

    return client_fd;
}

TcpConnection::TcpConnection(int fd) : fd_(fd) {}

TcpConnection::~TcpConnection() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

ssize_t TcpConnection::read(void* buf, std::size_t len) {
    while (true) {
        ssize_t n = ::read(fd_, buf, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;  // 인터럽트되면 재시도
            }
            return -1;
        }
        return n;
    }
}

ssize_t TcpConnection::write(const void* buf, std::size_t len) {
    const auto* ptr = static_cast<const char*>(buf);
    std::size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = ::write(fd_, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) {
                continue;  // 인터럽트되면 재시도
            }
            return -1;
        }
        ptr += n;
        remaining -= static_cast<std::size_t>(n);
    }

    return static_cast<ssize_t>(len);
}

std::string TcpConnection::peer_ip() const {
    if (fd_ < 0) {
        return "";
    }

    sockaddr_in addr{};
    socklen_t len = sizeof(addr);

    if (::getpeername(fd_, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        return "";
    }

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
    return std::string(ip_str);
}

}  // namespace veda::net

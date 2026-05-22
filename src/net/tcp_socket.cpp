// src/net/tcp_socket.cpp

#include "net/tcp_socket.hpp"

#include <unistd.h>

namespace veda::net {

TcpListener::TcpListener(uint16_t /*port*/) {
    // TODO (Week 1):
    //   socket(AF_INET, SOCK_STREAM, 0)
    //   setsockopt SO_REUSEADDR
    //   bind() to 0.0.0.0:port
    //   listen(backlog)
}

TcpListener::~TcpListener() {
    if (fd_ >= 0) ::close(fd_);
}

int TcpListener::accept_one() {
    // TODO: accept() and return client fd
    return -1;
}

TcpConnection::TcpConnection(int fd) : fd_(fd) {}

TcpConnection::~TcpConnection() {
    if (fd_ >= 0) ::close(fd_);
}

ssize_t TcpConnection::read(void* /*buf*/, std::size_t /*len*/) {
    // TODO: ::read with EINTR retry
    return -1;
}

ssize_t TcpConnection::write(const void* /*buf*/, std::size_t /*len*/) {
    // TODO: ::write loop until all bytes sent
    return -1;
}

}  // namespace veda::net

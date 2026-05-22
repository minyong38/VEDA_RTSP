// src/net/tcp_socket.hpp
//
// Thin RAII wrapper around POSIX TCP sockets. The point is to centralize
// error handling (errno -> exception or expected) rather than scattering
// socket()/bind()/accept() through the codebase.

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace veda::net {

class TcpListener {
public:
    explicit TcpListener(uint16_t port);
    ~TcpListener();

    TcpListener(const TcpListener&)            = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    // Blocks until a client connects. Returns the client fd, or -1 on error.
    int accept_one();

    int fd() const { return fd_; }

private:
    int fd_ = -1;
};

class TcpConnection {
public:
    explicit TcpConnection(int fd);
    ~TcpConnection();

    TcpConnection(const TcpConnection&)            = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    ssize_t read(void* buf, std::size_t len);
    ssize_t write(const void* buf, std::size_t len);

    int fd() const { return fd_; }

private:
    int fd_ = -1;
};

}  // namespace veda::net

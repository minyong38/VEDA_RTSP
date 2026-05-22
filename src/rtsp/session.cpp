// src/rtsp/session.cpp

#include "rtsp/session.hpp"

namespace veda::rtsp {

Session::Session(int client_fd) : client_fd_(client_fd) {}

Session::~Session() {
    // TODO: close socket if still open, tear down RTP transport
}

void Session::serve() {
    // TODO (Week 2-3):
    //   - read() loop on client_fd_
    //   - parse_request() each message
    //   - dispatch on method + current state_
    //   - PLAY transitions to PLAYING and kicks off RTP streaming
}

}  // namespace veda::rtsp

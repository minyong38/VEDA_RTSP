// src/rtsp/session.hpp
//
// One Session per RTSP client. Owns the per-client state machine:
//
//        OPTIONS / DESCRIBE
//   INIT ───────────────────► READY
//                              │  SETUP
//                              ▼
//                            READY ──── PLAY ────► PLAYING
//                              ▲                     │
//                              └──── PAUSE ──────────┘
//                              │
//                              ▼  TEARDOWN
//                            CLOSED

#pragma once

#include <cstdint>
#include <string>

namespace veda::rtsp {

enum class State {
    INIT,
    READY,
    PLAYING,
    CLOSED,
};

class Session {
public:
    explicit Session(int client_fd);
    ~Session();

    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;

    // Reads and processes RTSP messages until the client disconnects.
    void serve();

    State state() const { return state_; }

private:
    int         client_fd_;
    State       state_   = State::INIT;
    std::string session_id_;        // assigned at SETUP
    uint16_t    client_rtp_port_ = 0;
};

}  // namespace veda::rtsp

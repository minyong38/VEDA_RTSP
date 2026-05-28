// src/rtsp/session.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// RTSP 세션 = 한 클라이언트의 연결 상태와 진행 단계를 들고 있는 객체.
// ─────────────────────────────────────────────────────────────────────────────
//
// [상태머신 (RFC 2326 §A.1)]
//
//        OPTIONS / DESCRIBE
//   INIT ───────────────────► INIT          (상태 안 바뀜)
//                              │
//                              │ SETUP
//                              ▼
//                            READY
//                              │     ┌──── PAUSE ─────┐
//                              │ PLAY│                │
//                              ▼     ▼                │
//                            PLAYING ─────────────────┘
//                              │
//                              │ TEARDOWN  (또는 READY에서 TEARDOWN)
//                              ▼
//                            CLOSED
//
// [각 상태에서 허용되는 메서드]
//   INIT     : OPTIONS, DESCRIBE, SETUP
//   READY    : OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN
//   PLAYING  : OPTIONS, DESCRIBE, PAUSE, TEARDOWN
//   CLOSED   : (메서드 받지 않음 — 소켓도 닫혀 있음)
//
//   허용 안 되는 메서드 → "455 Method Not Valid in This State"
//
// [현재 상태]
//   이 클래스 자체는 아직 비어 있다. 모든 처리가 Server::handle_client에
//   들어가 있는데, 멀티클라이언트로 확장할 때 (Week 5) 이쪽으로 옮길 예정.

#pragma once

#include <cstdint>
#include <string>

namespace veda::rtsp {

enum class State {
    INIT,      // 막 연결됨, 아직 SETUP 안 됨
    READY,     // SETUP 끝남, PLAY 대기 중
    PLAYING,   // RTP 스트리밍 중
    CLOSED,    // TEARDOWN 또는 연결 끊김
};

class Session {
public:
    explicit Session(int client_fd);
    ~Session();

    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;

    // 클라이언트가 끊길 때까지 RTSP 메시지를 읽어 처리하는 루프.
    void serve();

    State state() const { return state_; }

private:
    int         client_fd_;
    State       state_           = State::INIT;
    std::string session_id_;             // SETUP에서 발급되는 ID
    uint16_t    client_rtp_port_ = 0;    // SETUP에서 받는 클라이언트 포트
};

}  // namespace veda::rtsp

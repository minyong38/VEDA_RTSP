// src/rtsp/session.cpp
//
// ─────────────────────────────────────────────────────────────────────────────
// Session 클래스 — 아직 구현 안 됨. 자리만 잡아둔 상태.
// ─────────────────────────────────────────────────────────────────────────────
//
// 지금은 Server::handle_client가 세션 역할까지 다 한다. epoll 기반
// 멀티클라이언트로 가는 시점에 이 클래스가 활성화되며, 한 클라이언트당
// Session 인스턴스 하나가 메모리에 살아 있게 된다.
//
// 이때 추가될 책임:
//   - state_ 머신 전이 (INIT → READY → PLAYING → CLOSED)
//   - 잘못된 상태 메서드에 대해 455 응답
//   - PLAY 진입 시 RTP Streamer를 자기 멤버로 들고 있고, 별도 스레드 또는
//     이벤트 루프에서 NAL을 push
//   - TEARDOWN/끊김 시 RTP/카메라 자원 정리

#include "rtsp/session.hpp"

namespace veda::rtsp {

Session::Session(int client_fd) : client_fd_(client_fd) {}

Session::~Session() {
    // TODO: 소켓이 아직 열려 있으면 닫기, RTP 송신 자원 정리
}

void Session::serve() {
    // TODO (Week 2-3 이후):
    //   while (state_ != CLOSED):
    //     1) client_fd_에서 read() 루프로 \r\n\r\n까지 모으기
    //     2) parser로 분해
    //     3) 현재 state_와 메서드 조합 검사 → 허용 안 되면 455
    //     4) 허용되면 처리 후 state_ 전이
    //     5) PLAY 진입 시 카메라/RTP 가동 시작
}

}  // namespace veda::rtsp

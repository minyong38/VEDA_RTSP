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
//   INIT     : OPTIONS, DESCRIBE, SETUP, GET_PARAMETER
//   READY    : OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER
//   PLAYING  : OPTIONS, DESCRIBE, PAUSE, TEARDOWN, GET_PARAMETER
//   CLOSED   : (메서드 받지 않음 — 소켓도 닫혀 있음)
//
//   허용 안 되는 메서드 → "455 Method Not Valid in This State"
//   Session 헤더 누락/불일치 → "454 Session Not Found"
//   모르는 메서드 → "501 Not Implemented"
//
// [설계: 소켓을 직접 만지지 않는다]
//   Session은 "바이트를 받아서(feed) 응답 문자열을 내놓는(SendFn)" 순수
//   프로토콜 로직이다. 실제 read/write는 Server(epoll 루프)가 담당.
//   덕분에 단위 테스트에서 소켓 없이 요청 문자열만 밀어 넣어 상태 전이를
//   검증할 수 있다 (tests/test_session.cpp).
//
// [RTP와의 연결]
//   PLAY  : Streamer를 만들어 client_ip:client_rtp_port로 connect 후
//           media::Hub에 구독 — 이후 송출은 Hub의 펌프 스레드가 담당.
//   PAUSE : Hub에서 구독 해제 (Streamer는 보존 — 재PLAY 대비)
//   TEARDOWN/끊김 : 구독 해제 + Streamer 폐기

#pragma once

#include "rtsp/parser.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace veda::media { class Hub; }
namespace veda::rtp   { class Streamer; }

namespace veda::rtsp {

enum class State {
    INIT,      // 막 연결됨, 아직 SETUP 안 됨
    READY,     // SETUP 끝남, PLAY 대기 중
    PLAYING,   // RTP 스트리밍 중 (Hub 구독 상태)
    CLOSED,    // TEARDOWN 또는 연결 끊김
};

class Session {
public:
    // 응답 문자열을 클라이언트로 내보내는 함수. Server가 TCP write로 연결한다.
    using SendFn = std::function<void(const std::string&)>;

    // client_ip : RTP를 쏠 목적지 주소 (TCP 연결의 peer IP)
    // hub       : 미디어 분배 허브. nullptr이면 PLAY 시 500으로 응답 (테스트용)
    Session(std::string client_ip, media::Hub* hub, SendFn send);
    ~Session();

    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;

    // TCP에서 읽힌 바이트를 누적하고, 완성된 요청(\r\n\r\n)마다 처리해서
    // send로 응답을 내보낸다. 여러 요청이 한 번에 와도(파이프라이닝),
    // 한 요청이 쪼개져 와도 동작한다.
    // 반환값: false = 세션 종료(TEARDOWN 등) — 호출 측이 연결을 닫아야 함.
    bool feed(const char* data, std::size_t len);

    // 연결이 끊겼을 때 호출. TEARDOWN 없이 끊겨도 Hub 구독과 RTP 자원을
    // 정리한다 (좀비 구독 방지).
    void on_disconnect();

    State state() const { return state_; }
    const std::string& id() const { return session_id_; }

private:
    // 현재 상태에서 이 메서드가 허용되는가 (상태머신 표 그대로)
    bool method_allowed(Method m) const;

    // 요청 한 건 처리 → 응답 문자열. 빈 문자열이면 핸들러가 직접 send함.
    std::string handle(const Request& req);

    std::string handle_describe(const Request& req);
    std::string handle_setup(const Request& req);
    std::string handle_play(const Request& req);
    std::string handle_pause(const Request& req);
    std::string handle_teardown(const Request& req);

    // PLAY/PAUSE/TEARDOWN의 Session 헤더가 우리가 발급한 ID와 일치하는가
    bool session_header_matches(const Request& req) const;

    // Hub 구독 해제 + Streamer 정리. 몇 번을 불러도 안전.
    void stop_streaming();

    std::string client_ip_;
    media::Hub* hub_;
    SendFn      send_;

    std::string rx_buffer_;          // 부분 수신 누적 버퍼
    State       state_ = State::INIT;
    std::string session_id_;         // SETUP에서 발급
    int         client_rtp_port_  = 0;
    int         client_rtcp_port_ = 0;

    // shared_ptr인 이유: Hub의 펌프 스레드와 소유권을 공유한다.
    // unsubscribe가 돌아온 뒤에야 reset하므로 use-after-free가 없다.
    std::shared_ptr<rtp::Streamer> streamer_;
};

}  // namespace veda::rtsp

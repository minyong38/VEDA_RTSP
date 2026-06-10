// tests/test_session.cpp
//
// Session 상태머신 검증.
//
// Session은 소켓을 모르는 순수 프로토콜 로직이라(설계 의도), 요청 문자열을
// feed()에 밀어 넣고 SendFn으로 나온 응답을 검사하는 것만으로 상태 전이와
// 에러 코드(455/454/461/501)를 전부 테스트할 수 있다.
//
// hub는 nullptr로 둔다 — PLAY의 미디어 가동을 빼면 상태머신 검증에 미디어
// 계층이 필요 없고, 테스트가 카메라/파일 존재 여부에 좌우되지 않는다.

#include "rtsp/session.hpp"
#include "util/base64.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace veda::rtsp;

namespace {

// 테스트 하네스: Session 하나 + 그 세션이 내보낸 응답 기록.
struct Harness {
    std::vector<std::string> responses;
    Session session;

    Harness()
        : session("127.0.0.1", /*hub=*/nullptr,
                  [this](const std::string& r) { responses.push_back(r); }) {}

    // 요청 전체를 한 번에 feed. 반환값은 feed 그대로 (false = 세션 종료).
    bool send(const std::string& raw) {
        return session.feed(raw.data(), raw.size());
    }

    const std::string& last() const { return responses.back(); }
};

// "RTSP/1.0 455 ..." → 455
int status_of(const std::string& response) {
    return std::stoi(response.substr(9, 3));
}

// SETUP 응답에서 "Session: XXXXXXXX" 값을 뽑는다.
std::string session_id_of(const std::string& response) {
    auto pos = response.find("Session: ");
    assert(pos != std::string::npos);
    pos += 9;
    auto end = response.find("\r\n", pos);
    return response.substr(pos, end - pos);
}

std::string options_req(int cseq) {
    return "OPTIONS rtsp://localhost:8554/stream RTSP/1.0\r\n"
           "CSeq: " + std::to_string(cseq) + "\r\n\r\n";
}

std::string setup_req(int cseq, const std::string& transport =
                                    "RTP/AVP;unicast;client_port=5000-5001") {
    return "SETUP rtsp://localhost:8554/stream/track0 RTSP/1.0\r\n"
           "CSeq: " + std::to_string(cseq) + "\r\n"
           "Transport: " + transport + "\r\n\r\n";
}

std::string with_session(const std::string& method, int cseq,
                         const std::string& session_id) {
    return method + " rtsp://localhost:8554/stream RTSP/1.0\r\n"
           "CSeq: " + std::to_string(cseq) + "\r\n"
           "Session: " + session_id + "\r\n\r\n";
}

}  // namespace

// ─── INIT 상태에서 허용 안 되는 메서드는 455 ─────────────────────────────────
void test_state_enforcement_in_init() {
    Harness h;

    assert(h.send("PLAY rtsp://localhost:8554/stream RTSP/1.0\r\nCSeq: 1\r\n\r\n"));
    assert(status_of(h.last()) == 455);
    assert(h.session.state() == State::INIT);

    assert(h.send("PAUSE rtsp://localhost:8554/stream RTSP/1.0\r\nCSeq: 2\r\n\r\n"));
    assert(status_of(h.last()) == 455);

    assert(h.send("TEARDOWN rtsp://localhost:8554/stream RTSP/1.0\r\nCSeq: 3\r\n\r\n"));
    assert(status_of(h.last()) == 455);
    assert(h.session.state() == State::INIT);

    std::cout << "[PASS] test_state_enforcement_in_init\n";
}

// ─── OPTIONS/DESCRIBE는 상태를 바꾸지 않는다 ────────────────────────────────
void test_options_describe_keep_state() {
    Harness h;

    assert(h.send(options_req(1)));
    assert(status_of(h.last()) == 200);
    assert(h.session.state() == State::INIT);

    assert(h.send("DESCRIBE rtsp://localhost:8554/stream RTSP/1.0\r\n"
                  "CSeq: 2\r\nAccept: application/sdp\r\n\r\n"));
    assert(status_of(h.last()) == 200);
    assert(h.last().find("application/sdp") != std::string::npos);
    assert(h.last().find("m=video") != std::string::npos);
    assert(h.session.state() == State::INIT);

    std::cout << "[PASS] test_options_describe_keep_state\n";
}

// ─── SETUP: INIT → READY, 세션 ID 발급, 포트 메아리 ─────────────────────────
void test_setup_transitions_to_ready() {
    Harness h;

    assert(h.send(setup_req(1)));
    assert(status_of(h.last()) == 200);
    assert(h.session.state() == State::READY);
    assert(h.last().find("client_port=5000-5001") != std::string::npos);
    assert(!session_id_of(h.last()).empty());

    std::cout << "[PASS] test_setup_transitions_to_ready\n";
}

// ─── SETUP 에러: Transport 누락 → 400, 형식 불량 → 461 ──────────────────────
void test_setup_transport_errors() {
    Harness h;

    assert(h.send("SETUP rtsp://localhost:8554/stream/track0 RTSP/1.0\r\n"
                  "CSeq: 1\r\n\r\n"));
    assert(status_of(h.last()) == 400);
    assert(h.session.state() == State::INIT);  // 실패한 SETUP은 전이 없음

    assert(h.send(setup_req(2, "RTP/AVP;unicast")));  // client_port 없음
    assert(status_of(h.last()) == 461);
    assert(h.session.state() == State::INIT);

    assert(h.send(setup_req(3, "RTP/AVP;unicast;client_port=abc")));
    assert(status_of(h.last()) == 461);

    std::cout << "[PASS] test_setup_transport_errors\n";
}

// ─── Session 헤더 검증: 누락/불일치 → 454 ───────────────────────────────────
void test_session_id_mismatch() {
    Harness h;

    assert(h.send(setup_req(1)));
    std::string sid = session_id_of(h.last());

    // Session 헤더 없이 PLAY
    assert(h.send("PLAY rtsp://localhost:8554/stream RTSP/1.0\r\nCSeq: 2\r\n\r\n"));
    assert(status_of(h.last()) == 454);
    assert(h.session.state() == State::READY);

    // 엉뚱한 세션 ID로 PLAY
    assert(h.send(with_session("PLAY", 3, "DEADBEEF")));
    assert(status_of(h.last()) == 454);
    assert(h.session.state() == State::READY);

    // 엉뚱한 세션 ID로 TEARDOWN
    assert(h.send(with_session("TEARDOWN", 4, "DEADBEEF")));
    assert(status_of(h.last()) == 454);
    assert(h.session.state() == State::READY);

    (void)sid;
    std::cout << "[PASS] test_session_id_mismatch\n";
}

// ─── TEARDOWN: READY → CLOSED, feed가 false 반환 ────────────────────────────
void test_teardown_closes_session() {
    Harness h;

    assert(h.send(setup_req(1)));
    std::string sid = session_id_of(h.last());

    // PAUSE는 READY에서 허용 안 됨 (PLAYING 전용)
    assert(h.send(with_session("PAUSE", 2, sid)));
    assert(status_of(h.last()) == 455);

    bool keep_open = h.send(with_session("TEARDOWN", 3, sid));
    assert(!keep_open);  // false = 호출 측(서버)이 연결을 닫아야 함
    assert(status_of(h.last()) == 200);
    assert(h.session.state() == State::CLOSED);

    std::cout << "[PASS] test_teardown_closes_session\n";
}

// ─── 모르는 메서드 → 501 ────────────────────────────────────────────────────
void test_unknown_method() {
    Harness h;

    assert(h.send("ANNOUNCE rtsp://localhost:8554/stream RTSP/1.0\r\nCSeq: 1\r\n\r\n"));
    assert(status_of(h.last()) == 501);

    std::cout << "[PASS] test_unknown_method\n";
}

// ─── GET_PARAMETER keep-alive: 어느 상태에서나 200 ──────────────────────────
void test_get_parameter_keepalive() {
    Harness h;

    assert(h.send("GET_PARAMETER rtsp://localhost:8554/stream RTSP/1.0\r\n"
                  "CSeq: 1\r\n\r\n"));
    assert(status_of(h.last()) == 200);

    assert(h.send(setup_req(2)));
    std::string sid = session_id_of(h.last());

    assert(h.send(with_session("GET_PARAMETER", 3, sid)));
    assert(status_of(h.last()) == 200);
    // 세션이 있으면 Session 헤더를 메아리쳐야 한다
    assert(h.last().find("Session: " + sid) != std::string::npos);

    std::cout << "[PASS] test_get_parameter_keepalive\n";
}

// ─── TCP 단편화: 요청이 쪼개져 와도 조립해서 처리 ───────────────────────────
void test_fragmented_request() {
    Harness h;

    std::string raw = options_req(1);
    std::string part1 = raw.substr(0, 20);
    std::string part2 = raw.substr(20);

    assert(h.session.feed(part1.data(), part1.size()));
    assert(h.responses.empty());  // 아직 \r\n\r\n 못 봄 — 응답 없어야 함

    assert(h.session.feed(part2.data(), part2.size()));
    assert(h.responses.size() == 1);
    assert(status_of(h.last()) == 200);

    std::cout << "[PASS] test_fragmented_request\n";
}

// ─── 파이프라이닝: 요청 두 개가 한 번에 와도 둘 다 처리 ─────────────────────
void test_pipelined_requests() {
    Harness h;

    std::string raw = options_req(1) + setup_req(2);
    assert(h.send(raw));
    assert(h.responses.size() == 2);
    assert(status_of(h.responses[0]) == 200);
    assert(status_of(h.responses[1]) == 200);
    assert(h.session.state() == State::READY);

    std::cout << "[PASS] test_pipelined_requests\n";
}

// ─── Base64 (SDP sprop-parameter-sets 재료) ─────────────────────────────────
void test_base64() {
    using veda::util::base64_encode;

    auto enc = [](const std::string& s) {
        return base64_encode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    };

    // RFC 4648 §10 테스트 벡터
    assert(enc("") == "");
    assert(enc("f") == "Zg==");
    assert(enc("fo") == "Zm8=");
    assert(enc("foo") == "Zm9v");
    assert(enc("foob") == "Zm9vYg==");
    assert(enc("fooba") == "Zm9vYmE=");
    assert(enc("foobar") == "Zm9vYmFy");

    std::cout << "[PASS] test_base64\n";
}

int run_all() {
    test_state_enforcement_in_init();
    test_options_describe_keep_state();
    test_setup_transitions_to_ready();
    test_setup_transport_errors();
    test_session_id_mismatch();
    test_teardown_closes_session();
    test_unknown_method();
    test_get_parameter_keepalive();
    test_fragmented_request();
    test_pipelined_requests();
    test_base64();
    return 0;
}

int main() {
    int rc = run_all();
    std::cout << "\nAll session tests passed!\n";
    return rc;
}

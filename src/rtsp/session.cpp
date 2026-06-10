// src/rtsp/session.cpp
//
// ─────────────────────────────────────────────────────────────────────────────
// 클라이언트 한 명의 RTSP 상태머신 구현.
// ─────────────────────────────────────────────────────────────────────────────
//
// Week 1~4까지 Server::handle_client에 흩어져 있던 요청 처리/세션 상태가
// 전부 이 클래스로 모였다. Server는 이제 epoll 이벤트만 나르고, "RTSP라는
// 프로토콜의 규칙"은 전부 여기서 강제된다.

#include "rtsp/session.hpp"

#include "media/hub.hpp"
#include "rtp/streamer.hpp"
#include "rtsp/response.hpp"
#include "sdp/builder.hpp"

#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace veda::rtsp {

namespace {

// rx 버퍼 상한. \r\n\r\n 없이 무한정 쌓이는 쓰레기 입력(또는 공격)으로부터
// 메모리를 보호한다. 정상 RTSP 요청은 수백 바이트면 충분.
constexpr std::size_t kMaxRxBuffer = 64 * 1024;

// 8자리 16진수 세션 ID 생성기.
// 진짜 보안용 ID라면 RNG의 시드와 출처를 더 엄격하게 관리해야 하지만,
// 학습 단계에서는 mt19937 + random_device로 충분.
std::string generate_session_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);

    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0')
        << std::setw(8) << dis(gen);
    return oss.str();
}

// Transport 헤더에서 client_port를 추출한다.
//
// 입력 예: "RTP/AVP;unicast;client_port=5000-5001"
// 출력  : client_rtp_port=5000, client_rtcp_port=5001
//
// 형식이 "5000"처럼 단일 포트면 RTCP는 자동으로 +1.
// 형식이 깨졌으면 false 반환 → 461 Unsupported Transport.
bool parse_transport(const std::string& transport,
                     int& client_rtp_port, int& client_rtcp_port) {
    auto pos = transport.find("client_port=");
    if (pos == std::string::npos) {
        return false;
    }

    pos += 12;  // "client_port=" 문자열 길이만큼 건너뛰기
    auto end = transport.find_first_of(";,", pos);  // 다음 구분자
    std::string ports = transport.substr(pos, end - pos);

    try {
        auto dash = ports.find('-');
        if (dash == std::string::npos) {
            // 단일 포트만 명시된 경우: "client_port=5000"
            client_rtp_port  = std::stoi(ports);
            client_rtcp_port = client_rtp_port + 1;
        } else {
            // 정상 케이스: "5000-5001"
            client_rtp_port  = std::stoi(ports.substr(0, dash));
            client_rtcp_port = std::stoi(ports.substr(dash + 1));
        }
    } catch (const std::exception&) {
        return false;  // 숫자가 아니거나 범위 초과
    }

    return client_rtp_port > 0 && client_rtp_port <= 65535;
}

// "Session: ABC123;timeout=60" 처럼 파라미터가 붙어 올 수 있으므로
// 첫 ';' 앞까지만 ID로 본다.
std::string session_id_of(std::string_view value) {
    auto semi = value.find(';');
    if (semi != std::string_view::npos) {
        value = value.substr(0, semi);
    }
    return std::string(value);
}

}  // namespace

Session::Session(std::string client_ip, media::Hub* hub, SendFn send)
    : client_ip_(std::move(client_ip)), hub_(hub), send_(std::move(send)) {}

Session::~Session() {
    // TEARDOWN 없이 객체가 사라져도 Hub 구독이 남지 않도록 보장.
    stop_streaming();
}

void Session::on_disconnect() {
    stop_streaming();
    state_ = State::CLOSED;
}

void Session::stop_streaming() {
    if (streamer_) {
        if (hub_) {
            // unsubscribe가 돌아오면 펌프 스레드는 더 이상 이 streamer를
            // 만지지 않는다 — 그 다음에 reset해야 안전하다.
            hub_->unsubscribe(streamer_);
        }
        streamer_.reset();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//                                feed()
// ═════════════════════════════════════════════════════════════════════════════
bool Session::feed(const char* data, std::size_t len) {
    rx_buffer_.append(data, len);

    // 비정상 입력으로 버퍼가 한없이 크면 연결을 끊는 게 안전하다.
    if (rx_buffer_.size() > kMaxRxBuffer) {
        std::cerr << "[Session] RX buffer overflow, closing\n";
        return false;
    }

    // 버퍼 안에 완성된 요청이 여러 개 있을 수 있다 (파이프라이닝).
    // \r\n\r\n 단위로 끊어 하나씩 처리한다.
    for (;;) {
        auto end_pos = rx_buffer_.find("\r\n\r\n");
        if (end_pos == std::string::npos) {
            return true;  // 아직 미완성 — 다음 read를 기다린다
        }

        std::string raw_request = rx_buffer_.substr(0, end_pos + 4);
        rx_buffer_.erase(0, end_pos + 4);

        std::cout << "\n[Request]\n" << raw_request << "\n";

        auto req = parse_request(raw_request);
        if (!req) {
            // 파싱 자체 실패: CSeq를 모르므로 0으로 응답. (이상적이지 않지만 OK)
            send_(Response::error(0, 400, "Bad Request"));
            continue;
        }

        std::string response = handle(*req);
        if (!response.empty()) {
            std::cout << "[Response]\n" << response << "\n";
            send_(response);
        }

        if (state_ == State::CLOSED) {
            return false;  // TEARDOWN 완료 — 호출 측이 소켓을 닫는다
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//                            상태머신 검사 + 디스패치
// ═════════════════════════════════════════════════════════════════════════════
bool Session::method_allowed(Method m) const {
    // 헤더 상단의 상태×메서드 표를 코드로 옮긴 것.
    // OPTIONS와 GET_PARAMETER(keep-alive)는 어느 상태에서나 무해하다.
    switch (m) {
        case Method::OPTIONS:
        case Method::GET_PARAMETER:
            return true;
        case Method::DESCRIBE:
            return true;
        case Method::SETUP:
            // PLAYING 중의 SETUP(트랜스포트 재협상)은 지원하지 않는다 → 455.
            return state_ == State::INIT || state_ == State::READY;
        case Method::PLAY:
            return state_ == State::READY;
        case Method::PAUSE:
            return state_ == State::PLAYING;
        case Method::TEARDOWN:
            return state_ == State::READY || state_ == State::PLAYING;
        case Method::UNKNOWN:
            return false;
    }
    return false;
}

std::string Session::handle(const Request& req) {
    if (req.method == Method::UNKNOWN) {
        return Response::error(req.cseq, 501, "Not Implemented");
    }
    if (!method_allowed(req.method)) {
        return Response::error(req.cseq, 455, "Method Not Valid in This State");
    }

    switch (req.method) {
        case Method::OPTIONS:
            return Response::options(req.cseq);
        case Method::DESCRIBE:
            return handle_describe(req);
        case Method::SETUP:
            return handle_setup(req);
        case Method::PLAY:
            return handle_play(req);
        case Method::PAUSE:
            return handle_pause(req);
        case Method::TEARDOWN:
            return handle_teardown(req);
        case Method::GET_PARAMETER:
            // keep-alive. 세션이 있으면 Session 헤더를 메아리쳐 준다.
            return session_id_.empty() ? Response::ok(req.cseq)
                                       : Response::ok(req.cseq, session_id_);
        case Method::UNKNOWN:
            break;  // 위에서 걸러짐
    }
    return Response::error(req.cseq, 500, "Internal Server Error");
}

// ═════════════════════════════════════════════════════════════════════════════
//                            메서드별 핸들러
// ═════════════════════════════════════════════════════════════════════════════

std::string Session::handle_describe(const Request& req) {
    sdp::VideoStreamInfo video_info;
    video_info.payload_type = 96;
    video_info.clock_rate   = 90000;
    // Hub가 스트림에서 SPS/PPS를 잡아뒀으면 sprop으로 광고 — 클라이언트가
    // 첫 키프레임을 기다리지 않고 디코더를 준비할 수 있다.
    // 비어 있어도 카메라 모드는 --inline이라 재생에는 지장 없다.
    video_info.sprop_parameter_sets = hub_ ? hub_->sprop_parameter_sets() : "";

    std::string sdp = sdp::build(video_info);
    // content_base 끝에 '/'를 붙여서, SETUP의 trackID URL을
    // 상대 경로로 해석할 수 있게 한다.
    return Response::describe(req.cseq, sdp, req.uri + "/");
}

std::string Session::handle_setup(const Request& req) {
    auto transport_hdr = header(req, "Transport");
    if (!transport_hdr) {
        return Response::error(req.cseq, 400, "Bad Request - No Transport");
    }

    if (!parse_transport(std::string(*transport_hdr),
                         client_rtp_port_, client_rtcp_port_)) {
        return Response::error(req.cseq, 461, "Unsupported Transport");
    }

    // 세션 ID는 최초 SETUP에서 발급. READY 상태의 재SETUP(포트 변경)은
    // 같은 세션으로 취급해 ID를 유지한다.
    if (session_id_.empty()) {
        session_id_ = generate_session_id();
    }

    // 서버 포트는 고정값으로 광고. 실제로 UDP bind를 하진 않고
    // 송신만 해서 큰 문제 없지만, 깐깐한 클라이언트는 SR/RR을
    // 그 포트로 보내려 할 수 있다.
    int server_rtp_port  = 6970;
    int server_rtcp_port = 6971;

    std::cout << "[SETUP] client_port=" << client_rtp_port_ << "-" << client_rtcp_port_
              << ", session=" << session_id_ << "\n";

    state_ = State::READY;
    return Response::setup(req.cseq, session_id_,
                           client_rtp_port_, client_rtcp_port_,
                           server_rtp_port, server_rtcp_port);
}

bool Session::session_header_matches(const Request& req) const {
    auto session_hdr = header(req, "Session");
    if (!session_hdr) {
        return false;
    }
    return session_id_of(*session_hdr) == session_id_;
}

std::string Session::handle_play(const Request& req) {
    if (!session_header_matches(req)) {
        return Response::error(req.cseq, 454, "Session Not Found");
    }

    // RTP 송출 준비: 스트리머를 만들어 클라이언트 주소에 연결.
    auto streamer = std::make_shared<rtp::Streamer>();
    if (!streamer->connect(client_ip_, static_cast<uint16_t>(client_rtp_port_))) {
        return Response::error(req.cseq, 500, "Internal Server Error");
    }

    // ─── 응답을 먼저 보낸다 ────────────────────────────────────────────────
    // Hub 구독 즉시 RTP가 흐르기 시작하므로, 클라이언트가 PLAY 200 OK를
    // 읽고 수신 준비를 마친 뒤에 첫 패킷이 도착하도록 순서를 보장한다.
    send_(Response::play(req.cseq, session_id_));

    if (!hub_ || !hub_->subscribe(streamer)) {
        // 응답은 이미 나갔지만 소스를 못 켰다 — 영상은 안 나간다.
        // (카메라 분리/파일 없음) 세션은 PLAYING으로 두지 않고 READY 유지.
        std::cerr << "[PLAY] Media source unavailable\n";
        return {};
    }

    streamer_ = std::move(streamer);
    state_ = State::PLAYING;
    std::cout << "[PLAY] Streaming to " << client_ip_ << ":" << client_rtp_port_
              << " (session " << session_id_ << ")\n";
    return {};  // 응답은 위에서 직접 보냈음
}

std::string Session::handle_pause(const Request& req) {
    if (!session_header_matches(req)) {
        return Response::error(req.cseq, 454, "Session Not Found");
    }

    stop_streaming();
    state_ = State::READY;
    std::cout << "[PAUSE] session " << session_id_ << " back to READY\n";
    return Response::ok(req.cseq, session_id_);
}

std::string Session::handle_teardown(const Request& req) {
    if (!session_header_matches(req)) {
        return Response::error(req.cseq, 454, "Session Not Found");
    }

    stop_streaming();
    state_ = State::CLOSED;
    std::cout << "[TEARDOWN] session " << session_id_ << " closed\n";
    return Response::teardown(req.cseq, session_id_);
}

}  // namespace veda::rtsp

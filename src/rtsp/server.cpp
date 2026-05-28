// src/rtsp/server.cpp
//
// ─────────────────────────────────────────────────────────────────────────────
// RTSP 서버 메인 로직. 이 파일이 모든 것을 한데 묶는 "오케스트레이터"다.
// ─────────────────────────────────────────────────────────────────────────────
//
// [핵심 흐름]
//   1) TCP 리스너 열기
//   2) accept로 클라이언트 한 명 받기
//   3) 그 클라이언트로부터 \r\n\r\n 단위로 RTSP 요청을 읽기
//   4) parser로 분해 → 메서드별로 응답 만들기 → write
//   5) PLAY를 받으면 카메라 켜고 RTP 루프 돌리기
//   6) TEARDOWN/연결끊김으로 종료
//
// [현재 한계]
//   - 단일 클라이언트만 처리 (PLAY 중에는 새 accept 못 함)
//   - 상태머신 강제 안 함 (SETUP 없이 PLAY 와도 455 안 줌)
//   - RTCP 없음, TCP interleaved 모드 없음
//   ─ 모두 Week 5 이후 작업.

#include "server.hpp"
#include "parser.hpp"
#include "response.hpp"
#include "sdp/builder.hpp"
#include "net/tcp_socket.hpp"
#include "rtp/streamer.hpp"
#include "camera/source.hpp"

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <random>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>

namespace veda::rtsp {

namespace {

// ─── 종료 시그널 처리 ───────────────────────────────────────────────────────
// Ctrl+C(SIGINT)를 받으면 메인 루프가 자연스럽게 빠져나오도록 플래그만 내린다.
// std::atomic을 쓰는 이유: 시그널 핸들러는 비동기적으로(다른 스레드처럼)
// 실행되므로 일반 bool에 쓰는 건 데이터 레이스. atomic은 안전하게 보장.
//
// 시그널 핸들러 안에서는 async-signal-safe 함수만 써야 하지만, std::cout은
// 엄밀히는 안전 보장이 없다. 학습용 코드라 일단 허용.
std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
    std::cout << "\n[Server] Shutting down...\n";
}

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

    return true;
}

}  // namespace

Server::Server(uint16_t port, std::string source_path)
    : port_(port), source_path_(std::move(source_path)) {}

Server::~Server() = default;

// ═════════════════════════════════════════════════════════════════════════════
//                                 run()
// ═════════════════════════════════════════════════════════════════════════════
int Server::run() {
    // Ctrl+C 처리 등록. SIGTERM도 같이 잡으면 더 견고하지만 일단 SIGINT만.
    std::signal(SIGINT, signal_handler);

    // 리스닝 소켓 (RAII).
    // 생성자에서 socket/bind/listen 다 처리. 실패하면 fd < 0.
    net::TcpListener listener(port_);
    if (listener.fd() < 0) {
        std::cerr << "[Server] Failed to start listener\n";
        return 1;
    }

    std::cout << "[Server] RTSP server started on port " << port_ << "\n";
    std::cout << "[Server] Waiting for connections... (Ctrl+C to stop)\n";

    // ─── 메인 accept 루프 ──────────────────────────────────────────────────
    while (g_running) {
        // 블로킹 accept. SIGINT가 오면 -1 + EINTR로 빠져나옴 → 다음 검사에서
        // g_running=false 보고 루프 종료.
        int client_fd = listener.accept_one();
        if (client_fd < 0) {
            if (!g_running) break;
            continue;  // 일시적 에러면 다음 accept 시도
        }

        // 한 명씩 직렬 처리. 이 함수가 돌아오기 전엔 다음 accept 못 함.
        handle_client(client_fd);
    }

    std::cout << "[Server] Server stopped\n";
    return 0;
}

// ═════════════════════════════════════════════════════════════════════════════
//                            handle_client()
// ═════════════════════════════════════════════════════════════════════════════
void Server::handle_client(int client_fd) {
    // accept된 fd를 RAII로 감싼다. 함수 끝에서 자동 close.
    net::TcpConnection conn(client_fd);
    char buffer[4096];
    std::string request_buffer;  // 부분 수신을 누적할 버퍼

    // 클라이언트 IP (RTP를 쏠 주소). getpeername으로 즉시 추출.
    std::string client_ip = conn.peer_ip();
    std::cout << "[Session] Handling client from " << client_ip << "\n";

    // ─── 세션 상태 ──────────────────────────────────────────────────────────
    // SETUP에서 채워지고 PLAY에서 사용된다. 원래는 Session 클래스에 들어가야
    // 할 자리지만, 지금은 함수 로컬로 둬도 한 클라이언트가 직렬 처리되므로 OK.
    int client_rtp_port = 0;
    std::string current_session_id;

    while (g_running) {
        // ─── 1. 소켓에서 일정량 읽기 ───────────────────────────────────────
        // TCP는 메시지 경계가 없으므로 한 번에 한 요청만 온다는 보장 없다.
        // 여러 요청이 한 번에 올 수도, 한 요청이 쪼개져 올 수도 있다.
        ssize_t n = conn.read(buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            // n=0: 클라이언트가 FIN 보냄 (정상 종료)
            // n<0: 에러
            std::cout << "[Session] Client disconnected\n";
            break;
        }
        buffer[n] = '\0';
        request_buffer += buffer;

        // ─── 2. 완전한 요청이 모였는지 확인 ────────────────────────────────
        // RTSP 메시지 끝 표시는 빈 줄(\r\n\r\n). 아직 못 모았으면 더 읽는다.
        // (본문 있는 메서드라면 추가로 Content-Length만큼 더 기다려야 하지만,
        //  현재 우리가 받는 요청들은 모두 본문 없음.)
        auto end_pos = request_buffer.find("\r\n\r\n");
        if (end_pos == std::string::npos) {
            continue;
        }

        // 한 요청을 떼어내고 버퍼 앞부분을 잘라낸다 (파이프라이닝 대비).
        std::string raw_request = request_buffer.substr(0, end_pos + 4);
        request_buffer = request_buffer.substr(end_pos + 4);

        std::cout << "\n[Request]\n" << raw_request << "\n";

        // ─── 3. 파싱 ───────────────────────────────────────────────────────
        auto req = parse_request(raw_request);
        if (!req) {
            // 파싱 자체 실패: CSeq를 모르므로 0으로 응답. (이상적이지 않지만 OK)
            auto response = Response::error(0, 400, "Bad Request");
            conn.write(response.data(), response.size());
            continue;
        }

        // ─── 4. 메서드별 응답 ──────────────────────────────────────────────
        std::string response;
        switch (req->method) {
            case Method::OPTIONS:
                // 지원 메서드 목록만 알려주면 끝.
                response = Response::options(req->cseq);
                break;

            case Method::DESCRIBE: {
                // SDP 본문을 만들어서 응답한다.
                // sprop_parameter_sets(SPS/PPS Base64)가 비어 있어서 일부
                // 클라이언트는 키프레임 올 때까지 디코딩 미루기도 함.
                // (TODO: 실제 카메라 스트림에서 SPS/PPS를 잡아서 채우기)
                sdp::VideoStreamInfo video_info;
                video_info.payload_type        = 96;
                video_info.clock_rate          = 90000;
                video_info.sprop_parameter_sets = "";

                std::string sdp = sdp::build(video_info);
                // content_base 끝에 '/'를 붙여서, SETUP의 trackID URL을
                // 상대 경로로 해석할 수 있게 한다.
                response = Response::describe(req->cseq, sdp, req->uri + "/");
                break;
            }

            case Method::SETUP: {
                // Transport 헤더 파싱.
                auto transport_hdr = header(*req, "Transport");
                if (!transport_hdr) {
                    response = Response::error(req->cseq, 400, "Bad Request - No Transport");
                    break;
                }

                int client_rtcp_port = 0;
                if (!parse_transport(std::string(*transport_hdr),
                                     client_rtp_port, client_rtcp_port)) {
                    response = Response::error(req->cseq, 461, "Unsupported Transport");
                    break;
                }

                // 세션 ID는 SETUP 시점에 발급. 이후 PLAY/TEARDOWN은 이 ID로 식별.
                current_session_id = generate_session_id();

                // 서버 포트는 고정값으로 광고. 실제로 UDP bind를 하진 않고
                // 송신만 해서 큰 문제 없지만, 깐깐한 클라이언트는 SR/RR을
                // 그 포트로 보내려 할 수 있다.
                int server_rtp_port  = 6970;
                int server_rtcp_port = 6971;

                std::cout << "[SETUP] client_port=" << client_rtp_port << "-" << client_rtcp_port
                          << ", server_port=" << server_rtp_port << "-" << server_rtcp_port
                          << ", session=" << current_session_id << "\n";

                response = Response::setup(req->cseq, current_session_id,
                                           client_rtp_port, client_rtcp_port,
                                           server_rtp_port, server_rtcp_port);
                break;
            }

            case Method::PLAY: {
                // ─── PLAY는 특수: 응답 보낸 뒤 RTP 루프로 들어간다 ─────────
                auto session_hdr = header(*req, "Session");
                if (!session_hdr) {
                    response = Response::error(req->cseq, 454, "Session Not Found");
                    break;
                }

                std::string session_id(*session_hdr);
                std::cout << "[PLAY] Starting stream for session: " << session_id << "\n";

                // 먼저 PLAY 200 OK를 보내야 클라이언트가 RTP를 수신할 준비를 함.
                response = Response::play(req->cseq, session_id);
                conn.write(response.data(), response.size());
                std::cout << "[Response]\n" << response << "\n";

                // ─── RTP 스트리밍 시작 ───────────────────────────────────
                if (client_rtp_port > 0) {
                    rtp::Streamer streamer;
                    if (streamer.connect(client_ip, static_cast<uint16_t>(client_rtp_port))) {
                        std::cout << "[RTP] Streaming to " << client_ip
                                  << ":" << client_rtp_port << "\n";

                        // 카메라 시작 (libcamera-vid 프로세스 popen).
                        camera::Source cam(640, 480, 30);
                        if (cam.start()) {
                            int frame_count = 0;

                            // ─── 카메라 → RTP 변환 루프 ────────────────────
                            // read_nal: 다음 NAL 하나가 도착하면 콜백 호출.
                            // 콜백 안에서 NAL을 RTP로 즉시 전송.
                            while (g_running) {
                                bool ok = cam.read_nal([&](const uint8_t* nal, std::size_t len) {
                                    streamer.send_nal(nal, len);

                                    // NAL 헤더의 하위 5비트가 NAL type.
                                    // type 1 = non-IDR slice (P 프레임)
                                    // type 5 = IDR slice (키프레임)
                                    // type 7/8 = SPS/PPS (프레임 아님 → ts 안 늘림)
                                    uint8_t nal_type = nal[0] & 0x1F;

                                    if (nal_type == 1 || nal_type == 5) {
                                        // 한 프레임이 끝났으므로 timestamp +3000
                                        // (90000 Hz / 30 fps = 3000 tick/frame)
                                        streamer.advance_timestamp(3000);
                                        frame_count++;
                                        if (frame_count % 30 == 0) {
                                            std::cout << "[RTP] Sent " << frame_count << " frames\n";
                                        }
                                    }
                                });

                                if (!ok) {
                                    break;  // EOF나 카메라 에러
                                }
                            }

                            cam.stop();
                            std::cout << "[RTP] Stream ended, total frames: " << frame_count << "\n";
                        } else {
                            std::cerr << "[RTP] Failed to start camera\n";
                        }
                    }
                }

                continue;  // 응답을 이미 보냈으므로 아래 공통 응답 코드 건너뜀
            }

            case Method::TEARDOWN:
                // 단순히 OK 응답하고 함수 즉시 종료 → conn 소멸자가 close().
                response = Response::ok(req->cseq);
                conn.write(response.data(), response.size());
                std::cout << "[Response]\n" << response << "\n";
                return;

            default:
                // PAUSE도 여기로 떨어진다 (아직 미구현).
                response = Response::error(req->cseq, 501, "Not Implemented");
                break;
        }

        // ─── 5. 공통 응답 전송 ─────────────────────────────────────────────
        std::cout << "[Response]\n" << response << "\n";
        conn.write(response.data(), response.size());
    }
}

}  // namespace veda::rtsp

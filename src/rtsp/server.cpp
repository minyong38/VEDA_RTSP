// src/rtsp/server.cpp

#include "server.hpp"
#include "parser.hpp"
#include "response.hpp"
#include "sdp/builder.hpp"
#include "net/tcp_socket.hpp"
#include "rtp/streamer.hpp"

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
    std::atomic<bool> g_running{true};

    void signal_handler(int) {
        g_running = false;
        std::cout << "\n[Server] Shutting down...\n";
    }

    // 랜덤 세션 ID 생성 (8자리 16진수)
    std::string generate_session_id() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);

        std::ostringstream oss;
        oss << std::hex << std::uppercase << std::setfill('0')
            << std::setw(8) << dis(gen);
        return oss.str();
    }

    // Transport 헤더에서 client_port 파싱
    // 예: "RTP/AVP;unicast;client_port=5000-5001"
    bool parse_transport(const std::string& transport,
                         int& client_rtp_port, int& client_rtcp_port) {
        auto pos = transport.find("client_port=");
        if (pos == std::string::npos) {
            return false;
        }

        pos += 12;  // "client_port=" 길이
        auto end = transport.find_first_of(";,", pos);
        std::string ports = transport.substr(pos, end - pos);

        auto dash = ports.find('-');
        if (dash == std::string::npos) {
            client_rtp_port = std::stoi(ports);
            client_rtcp_port = client_rtp_port + 1;
        } else {
            client_rtp_port = std::stoi(ports.substr(0, dash));
            client_rtcp_port = std::stoi(ports.substr(dash + 1));
        }

        return true;
    }
}

Server::Server(uint16_t port, std::string source_path)
    : port_(port), source_path_(std::move(source_path)) {}

Server::~Server() = default;

int Server::run() {
    // 시그널 핸들러 등록 (Ctrl+C)
    std::signal(SIGINT, signal_handler);

    // TCP 리스너 생성
    net::TcpListener listener(port_);
    if (listener.fd() < 0) {
        std::cerr << "[Server] Failed to start listener\n";
        return 1;
    }

    std::cout << "[Server] RTSP server started on port " << port_ << "\n";
    std::cout << "[Server] Waiting for connections... (Ctrl+C to stop)\n";

    while (g_running) {
        // 클라이언트 연결 대기
        int client_fd = listener.accept_one();
        if (client_fd < 0) {
            if (!g_running) break;  // 종료 시그널
            continue;
        }

        // 클라이언트 처리 (단일 스레드, 한 번에 하나씩)
        handle_client(client_fd);
    }

    std::cout << "[Server] Server stopped\n";
    return 0;
}

void Server::handle_client(int client_fd) {
    net::TcpConnection conn(client_fd);
    char buffer[4096];
    std::string request_buffer;

    // 클라이언트 IP 주소
    std::string client_ip = conn.peer_ip();
    std::cout << "[Session] Handling client from " << client_ip << "\n";

    // 세션 상태 (SETUP에서 설정, PLAY에서 사용)
    int client_rtp_port = 0;
    std::string current_session_id;

    while (g_running) {
        // 데이터 읽기
        ssize_t n = conn.read(buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            std::cout << "[Session] Client disconnected\n";
            break;
        }
        buffer[n] = '\0';
        request_buffer += buffer;

        // 완전한 요청인지 확인 (\r\n\r\n으로 끝나야 함)
        auto end_pos = request_buffer.find("\r\n\r\n");
        if (end_pos == std::string::npos) {
            continue;  // 아직 요청이 완전하지 않음
        }

        // 요청 파싱
        std::string raw_request = request_buffer.substr(0, end_pos + 4);
        request_buffer = request_buffer.substr(end_pos + 4);

        std::cout << "\n[Request]\n" << raw_request << "\n";

        auto req = parse_request(raw_request);
        if (!req) {
            auto response = Response::error(0, 400, "Bad Request");
            conn.write(response.data(), response.size());
            continue;
        }

        // 메서드별 응답 처리
        std::string response;
        switch (req->method) {
            case Method::OPTIONS:
                response = Response::options(req->cseq);
                break;

            case Method::DESCRIBE: {
                // SDP 생성
                sdp::VideoStreamInfo video_info;
                video_info.payload_type = 96;
                video_info.clock_rate = 90000;
                // TODO: 실제 스트림에서 SPS/PPS 추출
                video_info.sprop_parameter_sets = "";

                std::string sdp = sdp::build(video_info);
                response = Response::describe(req->cseq, sdp, req->uri + "/");
                break;
            }

            case Method::SETUP: {
                // Transport 헤더 파싱
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

                // 세션 ID 생성
                current_session_id = generate_session_id();

                // 서버 RTP/RTCP 포트 (고정값 사용)
                int server_rtp_port = 6970;
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
                // 세션 ID 확인
                auto session_hdr = header(*req, "Session");
                if (!session_hdr) {
                    response = Response::error(req->cseq, 454, "Session Not Found");
                    break;
                }

                std::string session_id(*session_hdr);
                std::cout << "[PLAY] Starting stream for session: " << session_id << "\n";

                // PLAY 응답 전송
                response = Response::play(req->cseq, session_id);
                conn.write(response.data(), response.size());
                std::cout << "[Response]\n" << response << "\n";

                // RTP 스트리밍 시작 (테스트용 더미 패킷)
                if (client_rtp_port > 0) {
                    rtp::Streamer streamer;
                    if (streamer.connect(client_ip, static_cast<uint16_t>(client_rtp_port))) {
                        std::cout << "[RTP] Sending test packets to " << client_ip
                                  << ":" << client_rtp_port << "\n";

                        // 테스트용 더미 NAL (IDR frame 시뮬레이션)
                        // NAL type 5 (IDR) = 0x65
                        std::vector<uint8_t> dummy_nal = {0x65, 0x00, 0x01, 0x02, 0x03};

                        // 5개의 테스트 프레임 전송
                        for (int i = 0; i < 5 && g_running; ++i) {
                            streamer.send_nal(dummy_nal.data(), dummy_nal.size());
                            streamer.advance_timestamp(3000);  // 30fps @ 90kHz
                            std::this_thread::sleep_for(std::chrono::milliseconds(33));
                            std::cout << "[RTP] Sent frame " << (i + 1) << "/5\n";
                        }

                        std::cout << "[RTP] Test stream complete\n";
                    }
                }

                continue;  // 이미 응답 전송함
            }

            case Method::TEARDOWN:
                response = Response::ok(req->cseq);
                conn.write(response.data(), response.size());
                std::cout << "[Response]\n" << response << "\n";
                return;  // 세션 종료

            default:
                response = Response::error(req->cseq, 501, "Not Implemented");
                break;
        }

        std::cout << "[Response]\n" << response << "\n";
        conn.write(response.data(), response.size());
    }
}

}  // namespace veda::rtsp

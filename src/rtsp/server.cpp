// src/rtsp/server.cpp
//
// ─────────────────────────────────────────────────────────────────────────────
// RTSP 서버 메인 로직 — epoll 이벤트 루프.
// ─────────────────────────────────────────────────────────────────────────────
//
// [핵심 흐름]
//   1) TCP 리스너 열기 + epoll에 등록
//   2) epoll_wait로 "일이 생긴 fd"만 받아온다
//   3) 리스너 이벤트  → accept → Client(연결+세션) 생성, epoll에 추가
//   4) 클라이언트 이벤트 → read → Session::feed (파싱·상태머신·응답은 세션 몫)
//   5) 끊김(EPOLLHUP/RDHUP, read 0) 또는 TEARDOWN → epoll 해제 + 자원 정리
//
// [Week 1~4와 달라진 점]
//   - 단일 accept 직렬 처리 → epoll 동시 처리 (PLAY 중에도 새 접속 수락)
//   - 요청 처리/상태머신이 Session 클래스로 이동 (455/454 강제 포함)
//   - RTP 송출이 media::Hub의 펌프 스레드로 이동 (카메라 1대 → N명 분배)

#include "server.hpp"

#include "media/hub.hpp"
#include "net/tcp_socket.hpp"
#include "rtsp/session.hpp"

#include <sys/epoll.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <iostream>

namespace veda::rtsp {

namespace {

// ─── 종료 시그널 처리 ───────────────────────────────────────────────────────
// Ctrl+C(SIGINT)를 받으면 메인 루프가 자연스럽게 빠져나오도록 플래그만 내린다.
// std::atomic을 쓰는 이유: 시그널 핸들러는 비동기적으로(다른 스레드처럼)
// 실행되므로 일반 bool에 쓰는 건 데이터 레이스. atomic은 안전하게 보장.
std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

constexpr int kMaxEvents = 64;

// epoll_wait 타임아웃(ms). 무한 대기 대신 주기적으로 깨어나 g_running을
// 확인한다 — 시그널이 epoll_wait가 아닌 다른 순간에 도착해도 0.5초 안에
// 종료가 시작되도록.
constexpr int kEpollTimeoutMs = 500;

}  // namespace

// ─── 클라이언트 한 명의 묶음 ────────────────────────────────────────────────
// conn이 session보다 먼저 선언돼야 한다: session 생성 시점에 peer_ip()를
// 쓰고, send 람다가 conn을 참조하기 때문 (멤버는 선언 순서대로 초기화).
struct Server::Client {
    net::TcpConnection conn;
    Session            session;

    Client(int fd, media::Hub* hub)
        : conn(fd),
          session(conn.peer_ip(), hub,
                  // Session이 만든 응답을 TCP로 흘려보내는 통로.
                  // Client는 unique_ptr로 보관되므로 this 주소가 안정적이다.
                  [this](const std::string& response) {
                      conn.write(response.data(), response.size());
                  }) {}
};

Server::Server(uint16_t port, std::string source_path)
    : port_(port), source_path_(std::move(source_path)) {}

Server::~Server() = default;

// ═════════════════════════════════════════════════════════════════════════════
//                                 run()
// ═════════════════════════════════════════════════════════════════════════════
int Server::run() {
    // Ctrl+C 처리 등록. SIGTERM도 같이 잡으면 더 견고하지만 일단 SIGINT만.
    std::signal(SIGINT, signal_handler);

    // 리스닝 소켓 (RAII). 생성자에서 socket/bind/listen 다 처리.
    net::TcpListener listener(port_);
    if (listener.fd() < 0) {
        std::cerr << "[Server] Failed to start listener\n";
        return 1;
    }

    // epoll 인스턴스. epoll_create1(0): 구식 epoll_create의 size 인자가
    // 무의미해진 현대식 버전.
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        std::cerr << "[Server] epoll_create1 failed\n";
        return 1;
    }

    // 리스너를 관심 목록에 추가. EPOLLIN = "accept할 연결이 도착했다".
    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = listener.fd();
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener.fd(), &ev) < 0) {
        std::cerr << "[Server] epoll_ctl(listener) failed\n";
        close(epoll_fd);
        return 1;
    }

    // 미디어 허브: 카메라/파일 소스 1개를 전 세션이 공유한다.
    media::SourceConfig cfg;
    cfg.source = source_path_;
    media::Hub hub(cfg);

    // fd → 클라이언트 묶음. unique_ptr이라 맵이 재해시돼도 Client 주소는
    // 고정 (send 람다가 this를 캡처하므로 중요).
    std::unordered_map<int, std::unique_ptr<Client>> clients;

    std::cout << "[Server] RTSP server started on port " << port_
              << " (source: " << source_path_ << ")\n";
    std::cout << "[Server] Waiting for connections... (Ctrl+C to stop)\n";

    // 클라이언트 제거 공통 루틴: epoll 해제 → 세션 정리(Hub 구독 해제) →
    // 맵에서 삭제 (TcpConnection 소멸자가 close).
    auto drop_client = [&](int fd) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
        auto it = clients.find(fd);
        if (it != clients.end()) {
            it->second->session.on_disconnect();
            clients.erase(it);
        }
        std::cout << "[Server] Client closed (active: " << clients.size() << ")\n";
    };

    // ─── 메인 이벤트 루프 ──────────────────────────────────────────────────
    epoll_event events[kMaxEvents];
    while (g_running) {
        int n = epoll_wait(epoll_fd, events, kMaxEvents, kEpollTimeoutMs);
        if (n < 0) {
            if (errno == EINTR) {
                continue;  // 시그널로 깨어남 — g_running 검사 후 재진입
            }
            std::cerr << "[Server] epoll_wait failed\n";
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            // ─── 새 연결 ───────────────────────────────────────────────────
            if (fd == listener.fd()) {
                int client_fd = listener.accept_one();
                if (client_fd < 0) {
                    continue;
                }

                auto client = std::make_unique<Client>(client_fd, &hub);

                epoll_event cev{};
                // EPOLLRDHUP: 상대가 보내기 방향을 닫음(half-close)도 감지.
                // 레벨 트리거(기본) 사용 — 한 이벤트당 read 한 번이면 충분하고,
                // 남은 데이터가 있으면 epoll이 다시 알려준다.
                cev.events  = EPOLLIN | EPOLLRDHUP;
                cev.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &cev) < 0) {
                    std::cerr << "[Server] epoll_ctl(client) failed\n";
                    continue;  // client 소멸 → fd close
                }

                std::cout << "[Server] Client connected from "
                          << client->conn.peer_ip()
                          << " (active: " << clients.size() + 1 << ")\n";
                clients.emplace(client_fd, std::move(client));
                continue;
            }

            // ─── 기존 클라이언트 이벤트 ────────────────────────────────────
            auto it = clients.find(fd);
            if (it == clients.end()) {
                // 같은 epoll_wait 배치에서 이미 제거된 fd일 수 있음
                continue;
            }

            if (events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                drop_client(fd);
                continue;
            }

            char buffer[4096];
            ssize_t r = it->second->conn.read(buffer, sizeof(buffer));
            if (r <= 0) {
                // 0 = 클라이언트가 FIN 보냄(정상 종료), <0 = 에러
                drop_client(fd);
                continue;
            }

            // 바이트를 세션에 먹인다. false = TEARDOWN 등으로 세션 종료.
            if (!it->second->session.feed(buffer, static_cast<std::size_t>(r))) {
                drop_client(fd);
            }
        }
    }

    std::cout << "\n[Server] Shutting down...\n";
    // clients가 먼저 정리되어 모든 세션이 Hub 구독을 해제한 뒤 hub가
    // 소멸해야 한다. 명시적으로 비워서 순서를 보장.
    clients.clear();
    close(epoll_fd);
    std::cout << "[Server] Server stopped\n";
    return 0;
}

}  // namespace veda::rtsp

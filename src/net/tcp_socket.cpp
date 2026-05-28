// src/net/tcp_socket.cpp
//
// ─────────────────────────────────────────────────────────────────────────────
// 이 파일은 POSIX TCP 소켓 API를 한 겹 감싸기만 한다. 진짜 일은 전부
// 커널이 하고, 우리는 그저 시스템콜을 정해진 순서대로 호출할 뿐이다.
// ─────────────────────────────────────────────────────────────────────────────

#include "tcp_socket.hpp"

#include <arpa/inet.h>     // inet_ntop, htons
#include <netinet/in.h>    // sockaddr_in, INADDR_ANY
#include <sys/socket.h>    // socket, bind, listen, accept, setsockopt
#include <unistd.h>        // close, read, write
#include <cerrno>          // errno
#include <cstring>         // strerror
#include <iostream>

namespace veda::net {

// ═════════════════════════════════════════════════════════════════════════════
//                               TcpListener
// ═════════════════════════════════════════════════════════════════════════════

TcpListener::TcpListener(uint16_t port) {
    // ─── 1단계: socket() ────────────────────────────────────────────────────
    //   AF_INET     : IPv4 주소 체계
    //   SOCK_STREAM : 신뢰성 있는 바이트 스트림 (= TCP)
    //   0           : 기본 프로토콜 (SOCK_STREAM이면 자동으로 IPPROTO_TCP)
    //
    // 반환값은 "파일 디스크립터" — 커널이 만든 소켓 객체를 가리키는 정수.
    // 이 정수 하나로 read/write/close 같은 파일 API를 그대로 쓸 수 있다.
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << "\n";
        return;
    }

    // ─── 2단계: SO_REUSEADDR 옵션 ───────────────────────────────────────────
    // 서버를 죽였다가 곧바로 다시 띄우면 "Address already in use" 에러가
    // 자주 난다. 이유: TCP는 정상 종료 후 TIME_WAIT 상태로 ~60초 머무는데,
    // 그동안은 같은 (IP, port) 조합으로 bind를 못 함.
    // SO_REUSEADDR은 "TIME_WAIT 상태라도 재사용 허용"이라고 커널에 알림.
    // 개발 중에는 거의 필수.
    int opt = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt() failed: " << std::strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return;
    }

    // ─── 3단계: bind() ──────────────────────────────────────────────────────
    // sockaddr_in 구조체에 "어느 IP, 어느 포트에 묶을지" 채워서 넘긴다.
    //   sin_family       : AF_INET (위 socket()과 일치해야 함)
    //   sin_addr.s_addr  : INADDR_ANY = 0.0.0.0 = "모든 네트워크 인터페이스"
    //                      (eth0, wlan0, lo 등 어느 쪽으로 들어와도 받음)
    //   sin_port         : 포트 번호. htons로 네트워크 바이트 오더(big-endian)
    //                      로 변환. x86은 little-endian이므로 변환 필수.
    //
    // {} 초기화는 구조체를 0으로 채운다(zero-init). 안 그러면 sin_zero 등
    // 패딩 필드에 쓰레기값이 남아 일부 커널에서 거부할 수 있다.
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    // sockaddr_in*을 sockaddr*로 캐스팅하는 이유: bind()는 IPv4/IPv6/UNIX 등
    // 여러 주소 체계를 받아야 해서 base 타입 sockaddr*를 받는다. C 시절의
    // "유사 다형성" 관용구.
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed: " << std::strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return;
    }

    // ─── 4단계: listen() ────────────────────────────────────────────────────
    // 이 시점에 소켓이 "passive(수동)" 상태로 바뀐다 = 연결을 받는 역할로 확정.
    // 두 번째 인자(backlog=5)는 "아직 accept하지 않은 연결을 큐에 몇 개까지
    // 쌓아둘지". 동시에 SYN이 5개 이상 몰리면 그 이상은 거부됨.
    if (::listen(fd_, 5) < 0) {
        std::cerr << "listen() failed: " << std::strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return;
    }

    std::cout << "[TCP] Listening on port " << port << "\n";
}

TcpListener::~TcpListener() {
    // RAII: 객체가 사라질 때 fd 회수. -1 검사는 생성 실패 케이스 보호.
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

int TcpListener::accept_one() {
    // accept()는 listen 큐에서 하나를 꺼내 "새로운 fd"를 만들어 돌려준다.
    // 리스닝 fd(fd_)는 계속 들고 다음 연결을 또 받을 수 있고, 반환된 fd는
    // 그 특정 클라이언트와의 양방향 통로로만 쓰인다.
    //
    // 인자 client_addr는 출력 파라미터: 커널이 "누가 연결했는지" 채워준다.
    // socklen_t는 in/out 둘 다 — 들어갈 땐 버퍼 크기, 나올 땐 실제 길이.
    sockaddr_in client_addr{};
    socklen_t   client_len = sizeof(client_addr);

    int client_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (client_fd < 0) {
        std::cerr << "accept() failed: " << std::strerror(errno) << "\n";
        return -1;
    }

    // 디버그용으로 누가 접속했는지 로그.
    // inet_ntop: binary IP → "1.2.3.4" 문자열 변환. 옛 inet_ntoa보다 스레드 안전.
    // ntohs: 포트도 네트워크 바이트 오더로 들어 있으므로 host 오더로 변환.
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
    std::cout << "[TCP] Client connected: " << ip_str
              << ":" << ntohs(client_addr.sin_port) << "\n";

    return client_fd;
}

// ═════════════════════════════════════════════════════════════════════════════
//                              TcpConnection
// ═════════════════════════════════════════════════════════════════════════════

TcpConnection::TcpConnection(int fd) : fd_(fd) {}

TcpConnection::~TcpConnection() {
    if (fd_ >= 0) {
        ::close(fd_);  // 커널에 "이쪽은 더 안 쓰니까 FIN 보내고 정리"
    }
}

ssize_t TcpConnection::read(void* buf, std::size_t len) {
    // TCP read 시스템콜은 다음 중 하나 발생 시 반환:
    //   - 데이터 도착   : >0 (실제 읽은 바이트, len과 같지 않을 수 있음)
    //   - 상대가 FIN   : 0 (정상 종료 신호)
    //   - 에러         : -1 (errno 참조)
    //
    // EINTR: 시스템콜 중 시그널이 도착하면 -1, errno=EINTR로 빠짐.
    //        이건 진짜 에러가 아니라 "방해받음"이므로 그냥 다시 시도.
    while (true) {
        ssize_t n = ::read(fd_, buf, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        return n;
    }
}

ssize_t TcpConnection::write(const void* buf, std::size_t len) {
    // TCP write의 함정: 한 번 호출했다고 len 전부를 보냈다는 보장이 없다.
    // 커널 송신 버퍼가 부족하거나 시그널이 끼면 부분 전송 후 돌아온다.
    // 그래서 보낸 만큼 포인터를 앞으로 밀고 남은 만큼만 다시 보내는 루프가
    // 필수. (TCP는 "스트림"이라 메시지 경계가 없다는 것도 같이 기억.)
    const auto* ptr       = static_cast<const char*>(buf);
    std::size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = ::write(fd_, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        ptr       += n;
        remaining -= static_cast<std::size_t>(n);
    }

    return static_cast<ssize_t>(len);
}

std::string TcpConnection::peer_ip() const {
    if (fd_ < 0) {
        return "";
    }

    // getpeername: "이 연결의 상대편 주소를 알려줘". accept 때도 받았지만
    // 거기선 sockaddr가 stack에 잠깐 머물고 사라졌으므로, 필요한 시점에
    // 커널에 다시 물어보는 패턴.
    sockaddr_in addr{};
    socklen_t   len = sizeof(addr);

    if (::getpeername(fd_, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        return "";
    }

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
    return std::string(ip_str);
}

}  // namespace veda::net

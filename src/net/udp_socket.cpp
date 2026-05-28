// src/net/udp_socket.cpp
//
// UDP 소켓 구현. TCP보다 단순한 이유:
//   - listen/accept가 없음 (연결 개념 자체가 없으니까)
//   - 부분 전송 처리 루프 없음 (데이터그램은 원자적)
//   - read 대신 보통 recvfrom (송신만 하면 필요 없음 → 우리 코드는 send 전용)

#include "net/udp_socket.hpp"

#include <unistd.h>        // close
#include <sys/socket.h>    // socket, send, connect, bind
#include <netinet/in.h>    // sockaddr_in
#include <arpa/inet.h>     // inet_pton, htons
#include <cstring>
#include <iostream>

namespace veda::net {

UdpSocket::UdpSocket() {
    // SOCK_DGRAM = 데이터그램 소켓 = UDP.
    // SOCK_STREAM(TCP)과 비교하면 같은 API, 다른 의미체계.
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        std::cerr << "[UdpSocket] Failed to create socket\n";
    }
}

UdpSocket::~UdpSocket() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool UdpSocket::bind(uint16_t port) {
    if (fd_ < 0) return false;

    // TCP의 bind와 사실상 동일. 차이는 "이 포트로 들어오는 데이터그램을
    // 이 소켓이 받겠다"는 의미만 갖는다 (listen/accept 단계 없음).
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[UdpSocket] Failed to bind to port " << port << "\n";
        return false;
    }

    return true;
}

bool UdpSocket::connect_to(const std::string& host, uint16_t port) {
    if (fd_ < 0) return false;

    // 보낼 대상 주소 구성.
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    // inet_pton: "presentation to network" — "192.168.0.1" 같은 문자열을
    // 32비트 정수로 변환. 반환값:
    //   1  : 성공
    //   0  : 형식이 IP 주소가 아님 (host 이름인 경우 등)
    //   -1 : 시스템 에러
    // 우리는 1이 아닌 경우 전부 에러로 본다. 호스트네임 해석은 별도(getaddrinfo).
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "[UdpSocket] Invalid address: " << host << "\n";
        return false;
    }

    // UDP에서 connect(): 핸드셰이크는 일어나지 않는다. 단지 커널이 이 소켓에
    // "기본 목적지"를 기록한다. 효과:
    //   1) 이후 send()만으로 그쪽에 전송 가능 (sendto의 dest 인자 생략)
    //   2) 다른 IP에서 온 데이터그램은 EAGAIN으로 거부
    //   3) ICMP 에러(예: "포트 도달 불가")가 send()에서 errno로 돌아옴
    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[UdpSocket] Failed to connect to " << host << ":" << port << "\n";
        return false;
    }

    return true;
}

ssize_t UdpSocket::send(const void* buf, std::size_t len) {
    if (fd_ < 0 || buf == nullptr) return -1;

    // send() vs sendto(): connect_to를 미리 했기 때문에 send()로 충분.
    // 마지막 인자(flags=0): MSG_DONTWAIT(논블로킹), MSG_MORE(뒤에 더 있음
    // 힌트) 같은 옵션을 줄 수 있지만 지금은 안 씀.
    //
    // 데이터그램은 한 번에 통째로 보내진다. len이 MTU를 넘으면 IP 레이어에서
    // 단편화되거나(보통) 드롭된다. RTP에서 우리는 그래서 페이로드를 1400 이내로
    // 미리 잘라둔다 (h264_fua.cpp 참고).
    return ::send(fd_, buf, len, 0);
}

}  // namespace veda::net

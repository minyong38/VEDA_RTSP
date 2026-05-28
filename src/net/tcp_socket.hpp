// src/net/tcp_socket.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// POSIX TCP 소켓의 얇은 RAII 래퍼.
// ─────────────────────────────────────────────────────────────────────────────
//
// [왜 만들었나]
//   socket()/bind()/listen()/accept()/close() 같은 C API를 RTSP 서버 코드에
//   직접 흩뿌리면 다음 두 가지가 지저분해진다:
//     1) errno 검사와 close() 호출이 매 함수마다 반복됨
//     2) fd가 누구 소유인지 헷갈리기 시작함 (이중 close 위험)
//   그래서 "리스닝 소켓"과 "수락된 연결"을 각각 클래스로 감싸고,
//   소멸자에서 close()를 보장하는 RAII 패턴을 적용한다.
//
// [TCP 서버 라이프사이클 복습]
//   socket()  : 커널에 새 파일 디스크립터 + 소켓 구조체 생성
//   bind()    : 그 소켓을 특정 (IP, port)에 묶음
//   listen()  : 수동(passive) 소켓으로 전환, 백로그 큐 활성화
//   accept()  : 백로그에 쌓인 연결을 꺼내 "새 fd"를 반환 (양방향 통신용)
//   close()   : 커널이 4-way FIN 핸드셰이크 시작
//
//   TcpListener  = socket+bind+listen+accept를 묶은 "문지기" 객체
//   TcpConnection = accept가 돌려준 클라이언트 전용 fd 래퍼
//
// [RAII = Resource Acquisition Is Initialization]
//   "자원의 획득은 객체 초기화 시점에" — 생성자에서 fd 열고, 소멸자에서
//   close. 예외/조기 return으로 함수가 빠져나가도 누수 안 남.

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace veda::net {

// ─── 리스닝 소켓 (서버 측, 연결을 받아들이는 쪽) ──────────────────────────────
class TcpListener {
public:
    // 생성자에서 socket→bind→listen까지 한 번에 처리한다.
    // 실패 시 fd_가 -1로 남기 때문에 호출 측에서 fd() < 0 검사 필요.
    explicit TcpListener(uint16_t port);
    ~TcpListener();

    // 복사 금지: fd는 단일 소유여야 한다. 두 객체가 같은 fd를 들고 있으면
    // 한쪽이 소멸할 때 close()하고, 다른 쪽은 죽은 fd를 쓰게 됨.
    TcpListener(const TcpListener&)            = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    // 클라이언트 한 명의 연결을 기다린다 (블로킹).
    // 반환값: 그 클라이언트와 통신할 새 fd (성공), -1 (실패)
    // 주의: 이 함수가 돌려주는 fd는 호출 측이 소유한다. 보통
    //       TcpConnection 객체에 넘겨서 RAII로 묶어둔다.
    int accept_one();

    int fd() const { return fd_; }

private:
    int fd_ = -1;  // -1 = "아직 안 열렸거나 실패한 상태"
};

// ─── 수락된 연결 (서버 측, 클라이언트 한 명과의 양방향 통로) ─────────────────
class TcpConnection {
public:
    // accept()로 받은 fd를 받아 소유권 인수.
    explicit TcpConnection(int fd);
    ~TcpConnection();

    TcpConnection(const TcpConnection&)            = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    // 한 번의 read()/write() 시스템콜로 감싼다.
    // - read: 0 = 정상 종료(상대가 FIN), <0 = 에러
    // - write: 부분 전송을 대비해 내부에서 루프 (전체 len을 다 보낼 때까지)
    ssize_t read(void* buf, std::size_t len);
    ssize_t write(const void* buf, std::size_t len);

    // 상대편 (클라이언트)의 IP 문자열 ("192.168.0.10" 형태)
    // RTP 전송 시 클라이언트로 패킷을 쏘려면 이 IP가 필요하다.
    std::string peer_ip() const;

    int fd() const { return fd_; }

private:
    int fd_ = -1;
};

}  // namespace veda::net

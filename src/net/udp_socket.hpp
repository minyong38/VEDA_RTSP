// src/net/udp_socket.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// RTP/RTCP 송신에 쓰는 UDP 소켓.
// ─────────────────────────────────────────────────────────────────────────────
//
// [TCP와 다른 점]
//   TCP: 연결 지향, 신뢰성 보장, 순서 보장, 흐름 제어 ─ 대신 느림
//   UDP: 데이터그램(독립 패킷) 단위, 손실/순서뒤바뀜 가능 ─ 대신 빠르고 단순
//
// [왜 영상은 UDP인가]
//   - 손실: 프레임 한 장 못 받아도 다음 키프레임 오면 복구 가능
//   - 지연: TCP는 손실된 바이트를 재전송할 때까지 뒤 바이트도 못 전달 (head-of-line
//     blocking). 실시간 영상에서 1초 늦게 도착한 프레임은 쓰레기.
//   - 시간 동기: RTP 자체에 sequence/timestamp가 있어 UDP가 잃은 패킷을
//     애플리케이션 레이어에서 감지 가능.
//
// [UDP 소켓에서 connect()가 왜 의미가 있나]
//   원래 UDP는 "연결 없음(connectionless)"인데, connect()를 호출하면 커널이
//   "기본 목적지 주소"를 그 소켓에 저장한다. 이후엔 send()만으로 그쪽으로
//   쏠 수 있고, 다른 호스트가 보낸 패킷은 무시된다. 보안과 코드 단순화 양득.

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace veda::net {

class UdpSocket {
public:
    // 생성자에서 socket()까지만 한다. bind나 connect는 별도 호출.
    UdpSocket();
    ~UdpSocket();

    UdpSocket(const UdpSocket&)            = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // 로컬 (서버) 포트에 묶기. 0을 넘기면 커널이 임시 포트를 자동 할당
    // (= ephemeral port). 우리 RTP 서버는 6970/6971을 쓰지만, 더 견고하게
    // 만들려면 ephemeral을 쓰고 SETUP 응답에 그 포트를 넣어 돌려주는 게 맞다.
    bool bind(uint16_t port);

    // 상대 (클라이언트) 주소를 "기본 목적지"로 저장.
    // 호출 후엔 send()만으로 매번 그쪽에 전송 가능.
    bool connect_to(const std::string& host, uint16_t port);

    // 단일 데이터그램 송신. 한 번에 한 RTP 패킷.
    // 반환값: 보낸 바이트 수 (보통 len과 같음), -1 = 에러.
    // 부분 전송이 없다 — TCP와 달리 UDP는 "전부 보내거나 실패하거나".
    ssize_t send(const void* buf, std::size_t len);

    int fd() const { return fd_; }

private:
    int fd_ = -1;
};

}  // namespace veda::net

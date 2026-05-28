// src/rtp/streamer.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// "NAL을 받으면 알맞게 RTP로 잘라서 UDP로 쏜다"는 단일 책임 객체.
// ─────────────────────────────────────────────────────────────────────────────
//
// [Streamer가 들고 있는 RTP 세션 상태]
//   - sequence  : 패킷마다 +1 (보안 권장사항대로 초기값 랜덤)
//   - timestamp : NAL 단위로는 동일, 프레임 끝나면 advance_timestamp로 증가
//   - SSRC      : 세션 시작 시 랜덤 32비트, 이후 고정
//   - PT        : 96 (H.264 동적 페이로드 타입)
//
// [세 번에 걸쳐 일어나는 분리]
//   1) 큰 NAL → 여러 FU-A 조각  (h264_fua.cpp)
//   2) 각 조각 → RTP 헤더 + 페이로드  (packetizer.cpp)
//   3) 그 패킷 → UDP send         (udp_socket.cpp)
//   Streamer는 이 셋을 조립해 "send_nal(nal)" 한 호출로 보이게 만든다.

#pragma once

#include "net/udp_socket.hpp"
#include "rtp/packetizer.hpp"
#include "rtp/h264_fua.hpp"

#include <cstdint>
#include <string>
#include <atomic>
#include <thread>
#include <memory>

namespace veda::rtp {

class Streamer {
public:
    // 생성자: SSRC와 시퀀스 초기값을 랜덤으로 잡는다 (RFC 3550 권장).
    Streamer();
    ~Streamer();

    Streamer(const Streamer&) = delete;
    Streamer& operator=(const Streamer&) = delete;

    // 클라이언트 IP/포트를 UDP 소켓의 기본 목적지로 설정.
    // 이후 send_nal()로 곧장 전송 가능.
    bool connect(const std::string& client_ip, uint16_t client_rtp_port);

    // NAL 한 개를 받아 RTP 패킷(들)로 변환해 전송.
    //   - nal[0]은 NAL 헤더 (start code 0x000001/00000001은 떼고 들어와야 함)
    //   - 1400 이하면 1패킷, 초과하면 FU-A로 자동 분할
    //   - 같은 NAL에서 나온 모든 패킷은 같은 timestamp, 마지막에만 marker=1
    bool send_nal(const uint8_t* nal, std::size_t nal_len);

    // 프레임이 끝났을 때 호출. 90kHz 클럭에서 30fps면 3000씩 증가.
    // 같은 프레임에 속한 NAL들은 같은 timestamp여야 하므로, "프레임 1장의
    // 마지막 NAL을 보낸 직후"에만 호출해야 한다.
    void advance_timestamp(uint32_t increment = 3000);

    // SSRC 직접 지정 (테스트용). 보통은 생성자가 랜덤으로 잡은 값 사용.
    void set_ssrc(uint32_t ssrc) { ssrc_ = ssrc; }
    uint32_t ssrc() const { return ssrc_; }

    // 상태 조회 (디버그용)
    bool     is_connected() const { return connected_; }
    uint16_t sequence()     const { return sequence_; }
    uint32_t timestamp()    const { return timestamp_; }

private:
    net::UdpSocket socket_;
    bool           connected_ = false;

    // ─── RTP 세션 상태 ─────────────────────────────────────────────────────
    uint16_t sequence_   = 0;            // 매 패킷마다 +1
    uint32_t timestamp_  = 0;            // 같은 NAL 내에서는 동일
    uint32_t ssrc_       = 0x12345678;   // 생성자가 랜덤으로 덮어씀
    uint8_t  payload_type_ = 96;         // H.264 동적 PT
};

}  // namespace veda::rtp

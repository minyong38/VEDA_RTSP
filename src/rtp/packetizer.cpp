// src/rtp/packetizer.cpp
//
// RTP 12바이트 헤더를 손으로 한 바이트씩 채운다. 이 파일은
// "tcpdump -X로 RTP 패킷 한 바이트씩 설명할 수 있다"는 목표의 핵심.
//
// [바이트 단위 검증법]
//   tshark -i lo -f "udp port 6970" -w out.pcap
//   tshark -r out.pcap -T fields -e data | head -1
//   첫 12바이트가 아래 코드 결과와 정확히 일치해야 한다.

#include "rtp/packetizer.hpp"

namespace veda::rtp {

// RTP 헤더는 CSRC가 없으면 정확히 12바이트.
constexpr std::size_t kRtpHeaderSize = 12;

void write_header(const Header& h, uint8_t* out) {
    // ─── Byte 0: V(2) | P(1) | X(1) | CC(4) ────────────────────────────────
    //
    //  비트:    7 6 5 4 3 2 1 0
    //  의미:    V V P X C C C C
    //  우리값:  1 0 0 0 0 0 0 0  →  0x80
    //
    //  V=2 ( 0b10 ), P=0, X=0, CC=0
    //  → 상위 2비트 "10"이 byte의 최상위로 가야 하므로 0x80.
    out[0] = 0x80;

    // ─── Byte 1: M(1) | PT(7) ──────────────────────────────────────────────
    //
    //  M은 최상위 비트(0x80), PT는 하위 7비트(0x7F).
    //  marker가 true면 M=1 → 0x80을 OR, false면 그대로.
    //  payload_type & 0x7F: 혹시 PT가 7비트 넘는 값이라도 마스크로 잘라 안전 확보.
    out[1] = static_cast<uint8_t>((h.marker ? 0x80 : 0x00) | (h.payload_type & 0x7F));

    // ─── Bytes 2-3: sequence (16 bits, big-endian) ─────────────────────────
    //
    //  htons() 대신 직접 분리:
    //    상위 8비트 = (seq >> 8) & 0xFF
    //    하위 8비트 =  seq       & 0xFF
    //  big-endian이므로 상위 바이트가 먼저(낮은 주소)에 온다.
    out[2] = static_cast<uint8_t>((h.sequence >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(h.sequence & 0xFF);

    // ─── Bytes 4-7: timestamp (32 bits, big-endian) ────────────────────────
    //
    //  4바이트를 상위부터 차곡차곡. htonl() 안 쓰고 직접 분리하는 이유는
    //  "어느 비트가 어디 가는지" 학습 차원에서 명확히 보이기 위함.
    out[4] = static_cast<uint8_t>((h.timestamp >> 24) & 0xFF);
    out[5] = static_cast<uint8_t>((h.timestamp >> 16) & 0xFF);
    out[6] = static_cast<uint8_t>((h.timestamp >>  8) & 0xFF);
    out[7] = static_cast<uint8_t>( h.timestamp        & 0xFF);

    // ─── Bytes 8-11: SSRC (32 bits, big-endian) ────────────────────────────
    //
    //  timestamp와 동일한 패턴. SSRC는 세션 동안 고정.
    out[8]  = static_cast<uint8_t>((h.ssrc >> 24) & 0xFF);
    out[9]  = static_cast<uint8_t>((h.ssrc >> 16) & 0xFF);
    out[10] = static_cast<uint8_t>((h.ssrc >>  8) & 0xFF);
    out[11] = static_cast<uint8_t>( h.ssrc        & 0xFF);
}

std::vector<uint8_t> build_packet(const Header& h,
                                  const uint8_t* payload,
                                  std::size_t payload_len) {
    // 헤더 12 + 페이로드 길이만큼 잡는다. 한 번에 정확한 크기로 할당해서
    // 이후 reallocation이 없도록 한다 (push_back 루프 대신).
    std::vector<uint8_t> packet(kRtpHeaderSize + payload_len);

    // 1. 헤더를 vector의 앞쪽 12바이트에 직접 쓰기.
    write_header(h, packet.data());

    // 2. 페이로드 복사. memcpy를 써도 되지만 std::copy가 더 C++다움.
    if (payload && payload_len > 0) {
        std::copy(payload, payload + payload_len, packet.begin() + kRtpHeaderSize);
    }

    return packet;
}

}  // namespace veda::rtp

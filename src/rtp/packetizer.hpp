// src/rtp/packetizer.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// RTP 패킷 헤더 작성 (RFC 3550).
// ─────────────────────────────────────────────────────────────────────────────
//
// [RTP 헤더 12바이트 비트 레이아웃]
//
//   0                   1                   2                   3
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |V=2|P|X|  CC   |M|     PT      |       sequence number         |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |                           timestamp                           |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |           synchronization source (SSRC) identifier            |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |            CSRC list (필요할 때만 0~15개, 우리는 안 씀)        |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// [각 필드 의미]
//   V (2 bits)   : 버전. RFC 3550 = 2.
//   P (1 bit)    : Padding. 끝에 패딩 바이트가 있다는 표시 (우리는 0).
//   X (1 bit)    : Header Extension. 확장 헤더 있음 표시 (우리는 0).
//   CC (4 bits)  : CSRC 개수. 믹서가 아니므로 0.
//   M (1 bit)    : Marker. 비디오에서는 "프레임의 마지막 패킷" 표시.
//   PT (7 bits)  : Payload Type. 동적이면 96~127 (H.264는 보통 96).
//   sequence (16): 패킷마다 +1. 손실/순서뒤바뀜 검출용.
//   timestamp(32): 미디어 시간. 90 kHz 클럭, 같은 프레임이면 동일.
//   SSRC (32)    : 송신자 식별자. 세션마다 랜덤하게 고정.
//
// [Big-endian]
//   네트워크 바이트 오더는 big-endian. 우리는 학습 목적으로 htons/htonl 대신
//   바이트 단위로 직접 시프트+마스크해서 채운다.

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace veda::rtp {

// 사용자가 채울 필드만 모아둔 입력 구조체.
// V/P/X/CC 등은 우리 구현이 항상 같은 값(0x80 byte 0)을 쓰므로 노출 안 한다.
struct Header {
    uint8_t  payload_type = 96;   // 7비트만 유효 (상위 비트는 marker 자리)
    uint16_t sequence     = 0;
    uint32_t timestamp    = 0;
    uint32_t ssrc         = 0;
    bool     marker       = false;
};

// out에 RTP 헤더 12바이트를 직접 쓴다. out 버퍼는 최소 12바이트 확보 필요.
// (호출 측이 build_packet 통해 쓰면 자동 처리됨)
void write_header(const Header& h, uint8_t* out);

// 헤더 + 페이로드를 합쳐 완전한 RTP 패킷을 vector로 반환.
// 짧은 패킷 한 번 보내는 단순 케이스에 편리하지만, 매번 vector 할당이 들어가서
// 고성능이 필요하면 write_header를 미리 잡아둔 버퍼에 직접 쓰는 게 낫다.
std::vector<uint8_t> build_packet(const Header& h,
                                  const uint8_t* payload,
                                  std::size_t payload_len);

}  // namespace veda::rtp

// src/rtp/h264_fua.cpp
//
// H.264 NAL → RTP 페이로드 묶음(들). 비트 단위 정확성이 중요한 함수이므로
// 비트 마스크와 시프트의 의미를 줄마다 설명한다.

#include "rtp/h264_fua.hpp"

namespace veda::rtp {

std::vector<std::vector<uint8_t>> fragment_nal(const uint8_t* nal,
                                               std::size_t nal_len,
                                               std::size_t mtu) {
    std::vector<std::vector<uint8_t>> fragments;

    // 방어 코드: null 또는 빈 입력이면 빈 리스트.
    if (nal == nullptr || nal_len == 0) {
        return fragments;
    }

    // ─── 케이스 1: NAL이 MTU 이내면 그대로 한 RTP에 담아 보낸다 ────────────
    //   = Single NAL Unit packet (RFC 6184 §5.6)
    //   RTP 페이로드 첫 바이트 = 원본 NAL 헤더, 그 뒤로 NAL 데이터.
    if (nal_len <= mtu) {
        fragments.emplace_back(nal, nal + nal_len);
        return fragments;
    }

    // ─── 케이스 2: NAL이 MTU보다 크면 FU-A로 분할 ──────────────────────────
    //
    // NAL 헤더 바이트 한 개를 두 개의 RTP 페이로드 헤더 바이트로 흩는다:
    //
    //   원본 NAL 헤더:
    //     bit 7   : F (forbidden_zero_bit, 항상 0)
    //     bit 6-5 : NRI (nal_ref_idc)
    //     bit 4-0 : Type (NAL 종류, 1=non-IDR, 5=IDR, 7=SPS, 8=PPS, ...)
    //
    //   → FU indicator : F | NRI | (Type=28)   ← Type 자리에 28 박음 (FU-A 표시)
    //   → FU header    : S | E | R=0 | (원본 Type)

    uint8_t nal_header = nal[0];
    uint8_t nal_type   = nal_header & 0x1F;   // 하위 5비트만: 0b0001_1111
    uint8_t fnri       = nal_header & 0xE0;   // 상위 3비트만: 0b1110_0000

    // FU indicator: (F|NRI) | 28
    //   28(0x1C) = 0b0001_1100 → Type 필드(5비트)에 28이 들어간 꼴
    //   fnri의 하위 5비트는 이미 0이므로 OR로 합쳐도 안전.
    uint8_t fu_indicator = fnri | 28;

    // ─── 페이로드 잘라낼 영역 ─────────────────────────────────────────────
    // 원본 NAL의 첫 바이트(NAL 헤더)는 위에서 정보 빼냈으니, 실제 분할 대상은
    // 그 다음 바이트부터.
    const uint8_t* payload    = nal + 1;
    std::size_t payload_len   = nal_len - 1;

    // 각 fragment 최대 페이로드:
    //   RTP 페이로드 = (FU indicator 1B) + (FU header 1B) + (실데이터)
    //   따라서 실데이터 최대 = mtu - 2
    std::size_t max_payload_per_fragment = mtu - 2;

    std::size_t offset   = 0;
    bool        is_first = true;

    while (offset < payload_len) {
        // 이번 조각이 담을 데이터 길이.
        std::size_t chunk_size = std::min(max_payload_per_fragment, payload_len - offset);
        bool        is_last    = (offset + chunk_size >= payload_len);

        // ─── FU header 구성 ────────────────────────────────────────────────
        //   하위 5비트 = 원본 Type
        //   bit 7 (S) = 첫 조각이면 1
        //   bit 6 (E) = 마지막 조각이면 1
        //   bit 5 (R) = 항상 0
        //
        //   주의: S와 E는 동시에 1이 될 수도 있다 (조각이 어쩌다 딱 하나로 끝난
        //         특수 케이스). 우리는 mtu 검사를 이미 했으므로 이 경로에 안 옴.
        uint8_t fu_header = nal_type;
        if (is_first) fu_header |= 0x80;   // 0b1000_0000
        if (is_last)  fu_header |= 0x40;   // 0b0100_0000

        // ─── 조각 바이트열 조립 ────────────────────────────────────────────
        //   [FU indicator][FU header][NAL 데이터 일부]
        std::vector<uint8_t> fragment;
        fragment.reserve(2 + chunk_size);          // 미리 잡아 재할당 방지
        fragment.push_back(fu_indicator);
        fragment.push_back(fu_header);
        fragment.insert(fragment.end(),
                        payload + offset,
                        payload + offset + chunk_size);

        fragments.push_back(std::move(fragment));

        offset   += chunk_size;
        is_first  = false;
    }

    return fragments;
}

}  // namespace veda::rtp

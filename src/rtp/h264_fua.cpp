// src/rtp/h264_fua.cpp

#include "rtp/h264_fua.hpp"

namespace veda::rtp {

std::vector<std::vector<uint8_t>> fragment_nal(const uint8_t* nal,
                                               std::size_t nal_len,
                                               std::size_t mtu) {
    std::vector<std::vector<uint8_t>> fragments;

    if (nal == nullptr || nal_len == 0) {
        return fragments;
    }

    // Case 1: NAL이 MTU보다 작으면 그대로 전송 (Single NAL Unit Packet)
    if (nal_len <= mtu) {
        fragments.emplace_back(nal, nal + nal_len);
        return fragments;
    }

    // Case 2: NAL이 MTU보다 크면 FU-A로 분할
    //
    // NAL header byte: F(1) | NRI(2) | Type(5)
    // FU indicator:    F(1) | NRI(2) | Type=28(5)  -- 28 = FU-A
    // FU header:       S(1) | E(1) | R(1) | Type(5)
    //
    // FU-A 오버헤드: 2바이트 (indicator + header)
    // 따라서 각 fragment의 최대 페이로드 = mtu - 2

    uint8_t nal_header = nal[0];
    uint8_t nal_type = nal_header & 0x1F;         // 하위 5비트
    uint8_t fnri = nal_header & 0xE0;             // F(1) + NRI(2)

    // FU indicator: fnri | type=28
    uint8_t fu_indicator = fnri | 28;

    // NAL 페이로드 (첫 번째 바이트는 NAL 헤더이므로 제외)
    const uint8_t* payload = nal + 1;
    std::size_t payload_len = nal_len - 1;

    // 각 fragment의 최대 페이로드 크기 (FU indicator + FU header = 2바이트)
    std::size_t max_payload_per_fragment = mtu - 2;

    std::size_t offset = 0;
    bool is_first = true;

    while (offset < payload_len) {
        std::size_t chunk_size = std::min(max_payload_per_fragment, payload_len - offset);
        bool is_last = (offset + chunk_size >= payload_len);

        // FU header: S(1) | E(1) | R(1) | Type(5)
        uint8_t fu_header = nal_type;  // 하위 5비트는 원본 NAL type
        if (is_first) {
            fu_header |= 0x80;  // S=1 (Start bit)
        }
        if (is_last) {
            fu_header |= 0x40;  // E=1 (End bit)
        }
        // R=0 (reserved)

        // Fragment 생성: FU indicator + FU header + payload chunk
        std::vector<uint8_t> fragment;
        fragment.reserve(2 + chunk_size);
        fragment.push_back(fu_indicator);
        fragment.push_back(fu_header);
        fragment.insert(fragment.end(), payload + offset, payload + offset + chunk_size);

        fragments.push_back(std::move(fragment));

        offset += chunk_size;
        is_first = false;
    }

    return fragments;
}

}  // namespace veda::rtp

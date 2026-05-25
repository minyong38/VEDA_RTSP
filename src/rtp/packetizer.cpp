// src/rtp/packetizer.cpp

#include "rtp/packetizer.hpp"

namespace veda::rtp {

// RTP 헤더는 12바이트 고정
constexpr std::size_t kRtpHeaderSize = 12;

void write_header(const Header& h, uint8_t* out) {
    // Byte 0: V=2 (version), P=0 (padding), X=0 (extension), CC=0 (CSRC count)
    // Binary: 10 0 0 0000 = 0x80
    out[0] = 0x80;

    // Byte 1: M (marker) | PT (payload type, 7 bits)
    out[1] = static_cast<uint8_t>((h.marker ? 0x80 : 0x00) | (h.payload_type & 0x7F));

    // Bytes 2-3: sequence number (network byte order = big-endian)
    out[2] = static_cast<uint8_t>((h.sequence >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(h.sequence & 0xFF);

    // Bytes 4-7: timestamp (network byte order)
    out[4] = static_cast<uint8_t>((h.timestamp >> 24) & 0xFF);
    out[5] = static_cast<uint8_t>((h.timestamp >> 16) & 0xFF);
    out[6] = static_cast<uint8_t>((h.timestamp >> 8) & 0xFF);
    out[7] = static_cast<uint8_t>(h.timestamp & 0xFF);

    // Bytes 8-11: SSRC (network byte order)
    out[8]  = static_cast<uint8_t>((h.ssrc >> 24) & 0xFF);
    out[9]  = static_cast<uint8_t>((h.ssrc >> 16) & 0xFF);
    out[10] = static_cast<uint8_t>((h.ssrc >> 8) & 0xFF);
    out[11] = static_cast<uint8_t>(h.ssrc & 0xFF);
}

std::vector<uint8_t> build_packet(const Header& h,
                                  const uint8_t* payload,
                                  std::size_t payload_len) {
    std::vector<uint8_t> packet(kRtpHeaderSize + payload_len);

    // RTP 헤더 작성
    write_header(h, packet.data());

    // 페이로드 복사
    if (payload && payload_len > 0) {
        std::copy(payload, payload + payload_len, packet.begin() + kRtpHeaderSize);
    }

    return packet;
}

}  // namespace veda::rtp

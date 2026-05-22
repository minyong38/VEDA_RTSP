// src/rtp/packetizer.hpp
//
// RTP packet header construction (RFC 3550).
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

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace veda::rtp {

struct Header {
    uint8_t  payload_type = 96;
    uint16_t sequence     = 0;
    uint32_t timestamp    = 0;
    uint32_t ssrc         = 0;
    bool     marker       = false;
};

// Writes 12-byte RTP header into `out` (must have at least 12 bytes).
void write_header(const Header& h, uint8_t* out);

// Convenience: build a full RTP packet (header + payload).
std::vector<uint8_t> build_packet(const Header& h,
                                  const uint8_t* payload,
                                  std::size_t payload_len);

}  // namespace veda::rtp

// src/rtp/h264_fua.hpp
//
// H.264 over RTP packetization (RFC 6184).
//
// A NAL unit larger than MTU must be split into FU-A fragments:
//   - First fragment:  FU indicator | FU header (S=1) | payload
//   - Middle fragments: FU indicator | FU header (S=0, E=0) | payload
//   - Last fragment:   FU indicator | FU header (E=1) | payload
//
// FU indicator byte = (original NAL F bit, NRI bits) | type=28
// FU header byte    = S | E | R=0 | original NAL type (5 bits)

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace veda::rtp {

constexpr std::size_t kDefaultMtu = 1400;  // safe under typical Ethernet MTU

// Splits a single NAL unit into one or more RTP-ready payloads.
// - If nal_len <= mtu, returns one element (Single NAL Unit packet).
// - Otherwise returns multiple FU-A fragments.
std::vector<std::vector<uint8_t>> fragment_nal(const uint8_t* nal,
                                               std::size_t nal_len,
                                               std::size_t mtu = kDefaultMtu);

}  // namespace veda::rtp

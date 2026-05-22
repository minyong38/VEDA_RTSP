// src/rtp/packetizer.cpp

#include "rtp/packetizer.hpp"

namespace veda::rtp {

void write_header(const Header& /*h*/, uint8_t* /*out*/) {
    // TODO (Week 3):
    //   - Byte 0:  V=2, P=0, X=0, CC=0
    //   - Byte 1:  M | payload_type
    //   - Bytes 2-3: sequence (network byte order)
    //   - Bytes 4-7: timestamp (network byte order)
    //   - Bytes 8-11: SSRC (network byte order)
}

std::vector<uint8_t> build_packet(const Header& /*h*/,
                                  const uint8_t* /*payload*/,
                                  std::size_t /*payload_len*/) {
    // TODO: header + payload concatenation
    return {};
}

}  // namespace veda::rtp

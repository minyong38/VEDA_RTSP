// src/rtp/h264_fua.cpp

#include "rtp/h264_fua.hpp"

namespace veda::rtp {

std::vector<std::vector<uint8_t>> fragment_nal(const uint8_t* /*nal*/,
                                               std::size_t /*nal_len*/,
                                               std::size_t /*mtu*/) {
    // TODO (Week 4):
    //   - If nal_len fits in MTU, emit a single NAL unit packet (just copy).
    //   - Otherwise:
    //       * Extract NAL header byte (F | NRI | type)
    //       * FU indicator = (F | NRI | 28)
    //       * Loop, marking S=1 on first chunk, E=1 on last chunk.
    return {};
}

}  // namespace veda::rtp

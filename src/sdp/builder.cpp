// src/sdp/builder.cpp

#include "sdp/builder.hpp"

namespace veda::sdp {

std::string build(const VideoStreamInfo& /*info*/) {
    // TODO (Week 2):
    //   - Assemble SDP lines with correct CRLF terminators
    //   - Include a=fmtp with packetization-mode=1 and sprop-parameter-sets
    return {};
}

}  // namespace veda::sdp

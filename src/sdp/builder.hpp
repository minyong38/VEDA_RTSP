// src/sdp/builder.hpp
//
// Builds the SDP body returned in DESCRIBE responses.
// Reference: RFC 4566 (SDP), RFC 6184 (H.264 payload format).
//
// Example output:
//   v=0
//   o=- 0 0 IN IP4 0.0.0.0
//   s=VEDA_RTSP
//   c=IN IP4 0.0.0.0
//   t=0 0
//   m=video 0 RTP/AVP 96
//   a=rtpmap:96 H264/90000
//   a=fmtp:96 packetization-mode=1; ...
//   a=control:trackID=0

#pragma once

#include <string>

namespace veda::sdp {

struct VideoStreamInfo {
    int    payload_type = 96;
    int    clock_rate   = 90000;
    // SPS/PPS (base64) — filled from the source H.264 stream.
    std::string sprop_parameter_sets;
};

std::string build(const VideoStreamInfo& info);

}  // namespace veda::sdp

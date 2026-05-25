// src/sdp/builder.cpp

#include "builder.hpp"

#include <sstream>

namespace veda::sdp {

std::string build(const VideoStreamInfo& info) {
    std::ostringstream oss;

    // v= (protocol version)
    oss << "v=0\r\n";

    // o= (origin)
    // o=<username> <sess-id> <sess-version> <nettype> <addrtype> <unicast-address>
    oss << "o=- 0 0 IN IP4 0.0.0.0\r\n";

    // s= (session name)
    oss << "s=VEDA RTSP Stream\r\n";

    // c= (connection information)
    oss << "c=IN IP4 0.0.0.0\r\n";

    // t= (timing)
    oss << "t=0 0\r\n";

    // a=control (aggregate control URL)
    oss << "a=control:*\r\n";

    // m= (media description)
    // m=<media> <port> <proto> <fmt>
    oss << "m=video 0 RTP/AVP " << info.payload_type << "\r\n";

    // a=rtpmap
    oss << "a=rtpmap:" << info.payload_type << " H264/" << info.clock_rate << "\r\n";

    // a=fmtp (format parameters)
    oss << "a=fmtp:" << info.payload_type << " packetization-mode=1";
    if (!info.sprop_parameter_sets.empty()) {
        oss << ";sprop-parameter-sets=" << info.sprop_parameter_sets;
    }
    oss << "\r\n";

    // a=control (track control URL)
    oss << "a=control:track0\r\n";

    return oss.str();
}

}  // namespace veda::sdp

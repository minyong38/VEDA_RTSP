// src/rtsp/parser.cpp

#include "rtsp/parser.hpp"

namespace veda::rtsp {

std::optional<Request> parse_request(std::string_view /*raw*/) {
    // TODO (Week 1):
    //   - Split request-line: METHOD URI RTSP/1.0
    //   - Split headers on \r\n until empty line
    //   - Extract CSeq (mandatory)
    return std::nullopt;
}

std::optional<std::string_view> header(const Request& /*req*/,
                                       std::string_view /*name*/) {
    // TODO: case-insensitive lookup
    return std::nullopt;
}

}  // namespace veda::rtsp

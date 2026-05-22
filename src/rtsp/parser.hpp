// src/rtsp/parser.hpp
//
// Parses RTSP request lines and headers. RTSP/1.0 (RFC 2326) reuses
// HTTP/1.1 syntax, so this is essentially a small HTTP-like parser
// scoped to the methods we support.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace veda::rtsp {

enum class Method {
    OPTIONS,
    DESCRIBE,
    SETUP,
    PLAY,
    PAUSE,
    TEARDOWN,
    UNKNOWN,
};

struct Request {
    Method                                       method = Method::UNKNOWN;
    std::string                                  uri;
    int                                          cseq = 0;
    std::unordered_map<std::string, std::string> headers;
};

// Returns std::nullopt on malformed input.
// `raw` is a single complete request (terminated by \r\n\r\n).
std::optional<Request> parse_request(std::string_view raw);

// Helper: case-insensitive lookup by header name.
std::optional<std::string_view> header(const Request& req, std::string_view name);

}  // namespace veda::rtsp

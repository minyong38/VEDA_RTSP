// src/rtsp/parser.cpp

#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace veda::rtsp {

namespace {

// 문자열 앞뒤 공백 제거
std::string_view trim(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return sv;
}

// 대소문자 무시 문자열 비교
bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// 메서드 문자열을 enum으로 변환
Method string_to_method(std::string_view method_str) {
    if (iequals(method_str, "OPTIONS"))  return Method::OPTIONS;
    if (iequals(method_str, "DESCRIBE")) return Method::DESCRIBE;
    if (iequals(method_str, "SETUP"))    return Method::SETUP;
    if (iequals(method_str, "PLAY"))     return Method::PLAY;
    if (iequals(method_str, "PAUSE"))    return Method::PAUSE;
    if (iequals(method_str, "TEARDOWN")) return Method::TEARDOWN;
    return Method::UNKNOWN;
}

// \r\n으로 다음 줄 찾기
std::string_view next_line(std::string_view& data) {
    auto pos = data.find("\r\n");
    if (pos == std::string_view::npos) {
        auto line = data;
        data = {};
        return line;
    }
    auto line = data.substr(0, pos);
    data.remove_prefix(pos + 2);  // \r\n 건너뛰기
    return line;
}

}  // namespace

std::optional<Request> parse_request(std::string_view raw) {
    if (raw.empty()) {
        return std::nullopt;
    }

    Request req;

    // 1. Request-Line 파싱: METHOD URI RTSP/1.0
    auto request_line = next_line(raw);
    if (request_line.empty()) {
        return std::nullopt;
    }

    // METHOD 추출
    auto space1 = request_line.find(' ');
    if (space1 == std::string_view::npos) {
        return std::nullopt;
    }
    auto method_str = request_line.substr(0, space1);
    req.method = string_to_method(method_str);

    // URI 추출
    auto rest = request_line.substr(space1 + 1);
    auto space2 = rest.find(' ');
    if (space2 == std::string_view::npos) {
        return std::nullopt;
    }
    req.uri = std::string(rest.substr(0, space2));

    // RTSP/1.0 버전 확인 (선택적)
    auto version = rest.substr(space2 + 1);
    if (version.substr(0, 5) != "RTSP/") {
        return std::nullopt;
    }

    // 2. 헤더 파싱
    while (!raw.empty()) {
        auto line = next_line(raw);

        // 빈 줄이면 헤더 끝
        if (line.empty()) {
            break;
        }

        // Header: Value 형식 파싱
        auto colon_pos = line.find(':');
        if (colon_pos == std::string_view::npos) {
            continue;  // 잘못된 헤더는 무시
        }

        auto key = trim(line.substr(0, colon_pos));
        auto value = trim(line.substr(colon_pos + 1));

        req.headers[std::string(key)] = std::string(value);
    }

    // 3. CSeq 추출 (필수 헤더)
    auto cseq_it = req.headers.find("CSeq");
    if (cseq_it == req.headers.end()) {
        // 대소문자 무시해서 다시 찾기
        for (const auto& [k, v] : req.headers) {
            if (iequals(k, "CSeq")) {
                req.cseq = std::stoi(v);
                break;
            }
        }
    } else {
        req.cseq = std::stoi(cseq_it->second);
    }

    return req;
}

std::optional<std::string_view> header(const Request& req, std::string_view name) {
    // 대소문자 무시하고 헤더 찾기
    for (const auto& [key, value] : req.headers) {
        if (iequals(key, name)) {
            return value;
        }
    }
    return std::nullopt;
}

}  // namespace veda::rtsp

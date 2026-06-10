// src/rtsp/parser.cpp
//
// RTSP 요청 파서 구현.
//
// [구현 전략]
//   라이브러리 없이 직접 자른다. string_view를 슬라이딩 윈도우처럼 굴리면서
//   \r\n 단위로 줄을 끊고, 줄 안에서 공백/:로 다시 자른다.
//
// [엣지 케이스 메모]
//   - 헤더 이름 대소문자: case-insensitive (CSeq == cseq)
//   - 헤더 값 앞뒤 공백: 무시 ("CSeq:  1  " → "1")
//   - 값에 ':' 포함 가능 (Authorization 등) → 첫 ':'만 분리 기준으로 사용
//   - 알 수 없는 헤더: 그냥 map에 넣고 무시 (호환성)

#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace veda::rtsp {

namespace {

// 문자열 앞뒤 공백/탭/CR/LF 제거.
// std::isspace는 char를 unsigned char로 캐스팅하지 않으면 음수 입력에서
// 정의되지 않은 동작이 되므로 항상 unsigned char로 변환해 넘긴다.
std::string_view trim(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return sv;
}

// 대소문자 무시 비교. tolower로 한 글자씩 정규화 후 비교.
// 길이 다르면 즉시 false.
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

// "OPTIONS" 같은 문자열을 Method enum으로 변환.
// 모르는 메서드는 UNKNOWN → 호출 측이 501 Not Implemented로 응답.
Method string_to_method(std::string_view method_str) {
    if (iequals(method_str, "OPTIONS"))  return Method::OPTIONS;
    if (iequals(method_str, "DESCRIBE")) return Method::DESCRIBE;
    if (iequals(method_str, "SETUP"))    return Method::SETUP;
    if (iequals(method_str, "PLAY"))     return Method::PLAY;
    if (iequals(method_str, "PAUSE"))    return Method::PAUSE;
    if (iequals(method_str, "TEARDOWN")) return Method::TEARDOWN;
    if (iequals(method_str, "GET_PARAMETER")) return Method::GET_PARAMETER;
    return Method::UNKNOWN;
}

// data에서 다음 \r\n 위치까지 한 줄을 떼어내고, data 자체는 그 다음 글자부터
// 시작하도록 잘라낸다 (입력 슬라이딩).
//
// 예: data = "abc\r\ndef\r\n"
//     반환    = "abc"
//     이후 data = "def\r\n"
std::string_view next_line(std::string_view& data) {
    auto pos = data.find("\r\n");
    if (pos == std::string_view::npos) {
        // \r\n이 없으면 남은 전부를 한 줄로 보고 data를 비운다.
        auto line = data;
        data = {};
        return line;
    }
    auto line = data.substr(0, pos);
    data.remove_prefix(pos + 2);  // \r\n 두 글자만큼 앞으로 이동
    return line;
}

}  // namespace

std::optional<Request> parse_request(std::string_view raw) {
    if (raw.empty()) {
        return std::nullopt;
    }

    Request req;

    // ─── 1. Request-Line 파싱: "METHOD URI RTSP/VERSION" ───────────────────
    //
    // 예: "OPTIONS rtsp://host/stream RTSP/1.0"
    //
    // 두 개의 공백을 기준으로 세 토큰으로 나눈다. 토큰 안에 공백이 없다는
    // 가정 (RTSP/HTTP 스펙상 그렇다).

    auto request_line = next_line(raw);
    if (request_line.empty()) {
        return std::nullopt;
    }

    // METHOD: 첫 공백 앞까지
    auto space1 = request_line.find(' ');
    if (space1 == std::string_view::npos) {
        return std::nullopt;  // 공백이 없으면 형식 오류
    }
    auto method_str = request_line.substr(0, space1);
    req.method = string_to_method(method_str);

    // URI: 첫 공백 뒤 ~ 두 번째 공백 앞
    auto rest = request_line.substr(space1 + 1);
    auto space2 = rest.find(' ');
    if (space2 == std::string_view::npos) {
        return std::nullopt;
    }
    req.uri = std::string(rest.substr(0, space2));

    // VERSION: 두 번째 공백 뒤. "RTSP/"로 시작해야 한다.
    // 우리는 1.0/2.0 구분까지는 안 본다 (모두 1.0으로 취급).
    auto version = rest.substr(space2 + 1);
    if (version.substr(0, 5) != "RTSP/") {
        return std::nullopt;
    }

    // ─── 2. 헤더 파싱 ──────────────────────────────────────────────────────
    //
    // 각 줄을 ':'로 자른다. 첫 ':' 앞이 key, 뒤가 value.
    // 빈 줄을 만나면 헤더 끝.

    while (!raw.empty()) {
        auto line = next_line(raw);

        if (line.empty()) {
            break;  // 빈 줄 = 헤더 영역 종료
        }

        auto colon_pos = line.find(':');
        if (colon_pos == std::string_view::npos) {
            continue;  // ':' 없는 잘못된 줄은 그냥 무시 (느슨한 파싱)
        }

        auto key   = trim(line.substr(0, colon_pos));
        auto value = trim(line.substr(colon_pos + 1));

        // 헤더 이름을 그대로 키로 쓰면 대소문자 다른 같은 헤더가 별개로 들어감.
        // 조회는 iequals로 하니까 일단 원본 그대로 저장만 한다.
        req.headers[std::string(key)] = std::string(value);
    }

    // ─── 3. CSeq 추출 ──────────────────────────────────────────────────────
    //
    // CSeq는 모든 RTSP 메시지에 필수인 순번 헤더. 응답에 그대로 메아리쳐야
    // 클라이언트가 요청-응답을 짝지을 수 있다.
    //
    // 먼저 정확히 "CSeq"로 찾아보고, 못 찾으면 대소문자 무시로 다시 찾는다.

    auto cseq_it = req.headers.find("CSeq");
    if (cseq_it == req.headers.end()) {
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
    // map의 키가 정확히 일치하지 않을 수 있으므로 선형 스캔.
    // 헤더 수가 수십 개 이하라 성능 문제는 없다.
    for (const auto& [key, value] : req.headers) {
        if (iequals(key, name)) {
            return value;
        }
    }
    return std::nullopt;
}

}  // namespace veda::rtsp

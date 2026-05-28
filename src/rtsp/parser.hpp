// src/rtsp/parser.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// RTSP 요청 메시지 파서.
// ─────────────────────────────────────────────────────────────────────────────
//
// [RTSP 메시지 모양 (RFC 2326, HTTP/1.1과 거의 동일)]
//
//   OPTIONS rtsp://host/stream RTSP/1.0\r\n   ← Request-Line
//   CSeq: 1\r\n                                ← 헤더들 (key: value)
//   User-Agent: ffplay\r\n
//   \r\n                                       ← 빈 줄 = 헤더 끝
//   [본문]                                     ← 메서드에 따라 있을 수도/없을 수도
//
//   - 줄 구분자: CRLF (\r\n) — HTTP와 동일
//   - 메시지 끝: 빈 줄 (\r\n\r\n)
//   - 본문이 있는 메서드(SET_PARAMETER 등)는 Content-Length 헤더로 길이 명시
//
// [파서가 하는 일은 단순]
//   1) 첫 줄을 공백 둘로 잘라 METHOD, URI, VERSION 분리
//   2) 나머지 줄을 ':'로 잘라 헤더 map에 넣기
//   3) CSeq 헤더만 별도로 int로 빼두기 (응답에 그대로 메아리쳐야 함)

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace veda::rtsp {

// 지원하는 RTSP 메서드.
// UNKNOWN은 파싱은 성공했지만 모르는 메서드인 경우 → 501 Not Implemented로 응답.
enum class Method {
    OPTIONS,    // "어떤 메서드 지원해?" 메뉴 요청
    DESCRIBE,   // "스트림 정보 줘" → SDP 본문 반환
    SETUP,      // "내 RTP 포트는 X야, 너의 포트 알려줘" → 세션 ID 발급
    PLAY,       // "재생 시작" → RTP 패킷 흘리기 시작
    PAUSE,      // "잠시 멈춰" (지금은 미구현)
    TEARDOWN,   // "연결 끊자" → 세션 정리
    UNKNOWN,
};

// 파싱된 요청 한 건의 표현.
struct Request {
    Method method = Method::UNKNOWN;
    std::string uri;                 // "rtsp://host/stream"
    int cseq = 0;                    // 순번 (응답에 그대로 돌려줘야 함)
    std::unordered_map<std::string, std::string> headers;
};

// raw 입력 한 덩어리(\r\n\r\n까지)를 파싱한다.
// 입력 형식이 깨졌으면 std::nullopt 반환 → 호출 측에서 400 Bad Request로 응답.
//
// 주의: string_view를 받으므로 raw의 수명이 함수 호출 동안 유지되어야 한다.
//       내부에서 헤더 값은 std::string으로 복사해 두므로 반환된 Request는
//       raw가 사라져도 안전.
std::optional<Request> parse_request(std::string_view raw);

// 헤더 조회 도우미. 대소문자 무시 비교.
// RTSP/HTTP 헤더 이름은 case-insensitive ("CSeq" == "cseq" == "CSEQ")이므로
// unordered_map의 기본 비교만으로는 부족하다.
std::optional<std::string_view> header(const Request& req, std::string_view name);

}  // namespace veda::rtsp

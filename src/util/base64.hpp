// src/util/base64.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// Base64 인코더 (RFC 4648).
// ─────────────────────────────────────────────────────────────────────────────
//
// [왜 필요한가]
//   SDP의 sprop-parameter-sets 항목은 SPS/PPS NAL의 "바이너리 원본"을
//   Base64 텍스트로 실어 나른다 (RFC 6184 §8.1). SDP는 줄 단위 텍스트
//   프로토콜이라 바이너리를 그대로 넣을 수 없기 때문.
//
// [Base64 원리 복습]
//   3바이트(24비트)를 6비트씩 4조각으로 잘라 각 조각을 64글자 알파벳
//   (A-Z a-z 0-9 + /)에 매핑. 입력이 3의 배수가 아니면 '='로 패딩.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace veda::util {

// data[0..len)을 Base64 문자열로 인코딩한다. len==0이면 빈 문자열.
std::string base64_encode(const uint8_t* data, std::size_t len);

}  // namespace veda::util

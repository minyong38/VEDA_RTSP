// src/util/base64.cpp
//
// 표준 Base64 (RFC 4648 §4). URL-safe 변형이 아닌 기본 알파벳을 쓴다 —
// SDP의 sprop-parameter-sets가 기대하는 형식이 이쪽이다.

#include "util/base64.hpp"

namespace veda::util {

std::string base64_encode(const uint8_t* data, std::size_t len) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    if (data == nullptr || len == 0) {
        return {};
    }

    std::string out;
    out.reserve(((len + 2) / 3) * 4);  // 출력 길이는 항상 4의 배수

    std::size_t i = 0;
    // 3바이트 묶음을 통째로 처리할 수 있는 구간
    for (; i + 3 <= len; i += 3) {
        uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                          (static_cast<uint32_t>(data[i + 1]) << 8) |
                          static_cast<uint32_t>(data[i + 2]);
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        out.push_back(kAlphabet[triple & 0x3F]);
    }

    // 꼬리 1~2바이트: '=' 패딩으로 4글자를 채운다
    std::size_t remain = len - i;
    if (remain == 1) {
        uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remain == 2) {
        uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                          (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        out.push_back('=');
    }

    return out;
}

}  // namespace veda::util

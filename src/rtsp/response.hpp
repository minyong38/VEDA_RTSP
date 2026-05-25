// src/rtsp/response.hpp
//
// RTSP 응답 메시지 생성기

#pragma once

#include <string>

namespace veda::rtsp {

class Response {
public:
    // 200 OK 기본 응답
    static std::string ok(int cseq);

    // OPTIONS 응답 - 지원하는 메서드 목록 반환
    static std::string options(int cseq);

    // DESCRIBE 응답 - SDP 본문 포함
    static std::string describe(int cseq, const std::string& sdp,
                                 const std::string& content_base);

    // SETUP 응답 - 세션 ID와 Transport 정보
    static std::string setup(int cseq, const std::string& session_id,
                              int client_rtp_port, int client_rtcp_port,
                              int server_rtp_port, int server_rtcp_port);

    // PLAY 응답
    static std::string play(int cseq, const std::string& session_id);

    // TEARDOWN 응답
    static std::string teardown(int cseq, const std::string& session_id);

    // 에러 응답
    static std::string error(int cseq, int status_code, const std::string& reason);
};

}  // namespace veda::rtsp

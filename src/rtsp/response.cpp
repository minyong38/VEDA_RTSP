// src/rtsp/response.cpp
//
// 응답 문자열 조립. RTSP 메시지는 HTTP와 같은 줄 단위 텍스트 프로토콜이라
// ostringstream으로 줄을 차곡차곡 쌓는 것만으로 충분하다.
//
// 주의 사항:
//   - 줄 구분자는 반드시 \r\n (LF만 쓰면 일부 클라이언트가 거부)
//   - 헤더 끝은 빈 줄 한 개 (\r\n\r\n)
//   - CSeq는 절대 빼먹지 말 것 — 클라이언트가 요청-응답 매칭의 기준으로 쓴다

#include "response.hpp"

#include <sstream>

namespace veda::rtsp {

std::string Response::ok(int cseq) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "\r\n";
    return oss.str();
}

std::string Response::ok(int cseq, const std::string& session_id) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Session: " << session_id << "\r\n"
        << "\r\n";
    return oss.str();
}

std::string Response::options(int cseq) {
    // Public 헤더: "이 서버가 받을 수 있는 메서드는 이것들이다"
    // ffplay는 이걸 보고 PAUSE를 보낼지 말지 결정하기도 함.
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN, GET_PARAMETER\r\n"
        << "\r\n";
    return oss.str();
}

std::string Response::describe(int cseq, const std::string& sdp,
                                const std::string& content_base) {
    // DESCRIBE 응답 = HTTP의 GET 응답과 거의 같은 패턴:
    //   - Content-Type: application/sdp (RFC 4566)
    //   - Content-Length: 정확한 본문 길이 (바이트 단위)
    //   - Content-Base: 이후 트랙 URL을 만들 때 기준
    //
    // Content-Length가 틀리면 클라이언트가 본문을 끝까지 안 읽거나, 다음
    // 메시지의 일부를 본문에 흡수해버리는 사고가 난다. sdp.size() 그대로 쓰자.
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Content-Type: application/sdp\r\n"
        << "Content-Base: " << content_base << "\r\n"
        << "Content-Length: " << sdp.size() << "\r\n"
        << "\r\n"
        << sdp;
    return oss.str();
}

std::string Response::setup(int cseq, const std::string& session_id,
                             int client_rtp_port, int client_rtcp_port,
                             int server_rtp_port, int server_rtcp_port) {
    // Transport 헤더 형식 (RFC 2326):
    //   RTP/AVP;unicast;client_port=X-Y;server_port=A-B
    //   - RTP/AVP        : RTP over UDP (RTP Audio/Video Profile)
    //   - unicast        : 멀티캐스트가 아닌 1:1 전송
    //   - client_port    : 클라이언트가 RTP/RTCP 받을 포트 쌍 (X=RTP, Y=RTCP)
    //   - server_port    : 서버가 RTP/RTCP 보낼/받을 포트 쌍
    //
    // 관습: RTP 포트는 짝수, RTCP 포트는 그 +1 (홀수)
    //
    // Session 헤더: 우리가 발급한 세션 ID. 이후 PLAY/PAUSE/TEARDOWN은
    // 이 ID를 헤더에 달고 와야 한다.
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Session: " << session_id << "\r\n"
        << "Transport: RTP/AVP;unicast;"
        << "client_port=" << client_rtp_port << "-" << client_rtcp_port << ";"
        << "server_port=" << server_rtp_port << "-" << server_rtcp_port << "\r\n"
        << "\r\n";
    return oss.str();
}

std::string Response::play(int cseq, const std::string& session_id) {
    // Range: NPT(Normal Play Time) 단위. "0.000-"은 "처음부터 끝까지".
    // 라이브 스트림은 끝이 없으므로 끝 값을 비운다.
    // 만약 녹화본을 seek 지원하려면 "20.000-40.000" 같은 식으로 보낼 수 있다.
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Session: " << session_id << "\r\n"
        << "Range: npt=0.000-\r\n"
        << "\r\n";
    return oss.str();
}

std::string Response::teardown(int cseq, const std::string& session_id) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Session: " << session_id << "\r\n"
        << "\r\n";
    return oss.str();
}

std::string Response::error(int cseq, int status_code, const std::string& reason) {
    // 자주 쓰는 코드:
    //   400 Bad Request        : 파싱 실패
    //   454 Session Not Found  : Session 헤더가 없거나 모르는 ID
    //   455 Method Not Valid   : 상태머신 위반 (예: SETUP 전에 PLAY)
    //   461 Unsupported Transport : Transport 헤더 파싱 실패
    //   501 Not Implemented    : 모르는 메서드
    std::ostringstream oss;
    oss << "RTSP/1.0 " << status_code << " " << reason << "\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "\r\n";
    return oss.str();
}

}  // namespace veda::rtsp

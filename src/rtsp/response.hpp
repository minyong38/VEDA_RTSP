// src/rtsp/response.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// RTSP 응답 메시지를 만드는 정적 빌더.
// ─────────────────────────────────────────────────────────────────────────────
//
// [RTSP 응답 모양]
//
//   RTSP/1.0 200 OK\r\n           ← Status-Line (버전 상태코드 사유)
//   CSeq: 1\r\n                   ← 요청의 CSeq를 그대로 메아리침 (필수)
//   <메서드별 헤더들>\r\n
//   \r\n                          ← 빈 줄 = 헤더 끝
//   <본문>                        ← DESCRIBE 응답만 SDP 본문이 따라옴
//
// [왜 클래스로 묶었나]
//   응답마다 헤더 조합이 조금씩 다르다 (Transport는 SETUP만, Session은
//   SETUP/PLAY/TEARDOWN, Content-Type/Length는 DESCRIBE 등). 매번 ostringstream을
//   직접 짜면 실수하기 쉬워서 메서드별 팩토리를 둬서 정형화한다.
//
//   상태가 없으므로 모두 static. 인스턴스를 만들 필요 없음.

#pragma once

#include <string>

namespace veda::rtsp {

class Response {
public:
    // 가장 단순한 200 OK (TEARDOWN 등 응답이 단순한 경우용)
    static std::string ok(int cseq);

    // Session 헤더를 메아리치는 200 OK (PAUSE, GET_PARAMETER keep-alive 등).
    // 세션이 있는 요청의 응답에는 Session 헤더를 같이 돌려주는 게 관례다.
    static std::string ok(int cseq, const std::string& session_id);

    // OPTIONS 응답: Public 헤더로 지원 메서드 목록 광고.
    // 클라이언트(ffplay/VLC)는 이 목록을 보고 어떤 메서드를 보낼지 결정.
    static std::string options(int cseq);

    // DESCRIBE 응답: SDP 본문 + Content-Length + Content-Base.
    // content_base는 이후 SETUP의 URI를 만들 때 기준이 되는 URL.
    // 예: content_base = "rtsp://host/stream/" 이면
    //     SETUP은 "rtsp://host/stream/trackID=0"으로 옴.
    static std::string describe(int cseq, const std::string& sdp,
                                 const std::string& content_base);

    // SETUP 응답: Session 헤더 발급 + Transport 헤더에 서버 RTP 포트 명시.
    // client_*_port: 요청에 들어온 클라이언트 포트를 메아리침
    // server_*_port: 우리가 RTP를 쏠 쪽 포트 (UDP bind 결과)
    static std::string setup(int cseq, const std::string& session_id,
                              int client_rtp_port, int client_rtcp_port,
                              int server_rtp_port, int server_rtcp_port);

    // PLAY 응답: Range 헤더로 재생 위치 광고. "npt=0.000-"은 처음부터 끝없이.
    static std::string play(int cseq, const std::string& session_id);

    // TEARDOWN 응답: Session 메아리만 하면 충분. 이후 TCP는 보통 닫힘.
    static std::string teardown(int cseq, const std::string& session_id);

    // 에러 응답: 400/454/461/501 등.
    static std::string error(int cseq, int status_code, const std::string& reason);
};

}  // namespace veda::rtsp

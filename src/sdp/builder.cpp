// src/sdp/builder.cpp
//
// SDP 본문 한 덩어리를 만든다. 단순한 텍스트 조립이지만, 각 줄의 의미와
// 순서가 모두 RFC 4566에 박혀 있으니 임의로 바꾸면 클라이언트가 거부한다.
//
// [디버깅 팁]
//   ffplay를 -loglevel debug로 띄우면 받은 SDP를 그대로 찍어준다.
//   거기서 누락된 줄이 있으면 즉시 보임.

#include "builder.hpp"

#include <sstream>

namespace veda::sdp {

std::string build(const VideoStreamInfo& info) {
    std::ostringstream oss;

    // v= : SDP 프로토콜 버전. 현재는 무조건 0.
    oss << "v=0\r\n";

    // o= : origin (세션의 출처) — username sess-id sess-version nettype addrtype unicast-address
    //   - "-"        : username 없음
    //   - "0 0"      : 세션 ID와 버전. 변경 시 클라이언트가 같은 세션의 갱신본임을 알 수 있음.
    //   - "IN IP4"   : 인터넷, IPv4
    //   - "0.0.0.0"  : 작성자 주소 (의미 없는 더미)
    oss << "o=- 0 0 IN IP4 0.0.0.0\r\n";

    // s= : 사람이 읽는 세션 이름. 표시용일 뿐 동작에는 영향 없음.
    oss << "s=VEDA RTSP Stream\r\n";

    // c= : connection info — nettype addrtype connection-address
    //   여기서 0.0.0.0은 "실제 주소는 SETUP의 Transport 헤더에서 결정한다"는 신호.
    //   (Unicast RTSP에서 흔한 관용구)
    oss << "c=IN IP4 0.0.0.0\r\n";

    // t= : 타이밍. "0 0"이면 영구 세션 (시작/종료 시간 미지정).
    oss << "t=0 0\r\n";

    // a=control:* (세션 레벨)
    //   집합 제어 URL. "*"는 "DESCRIBE에 쓴 URI 그대로 쓰라"는 의미.
    //   클라이언트가 PLAY/PAUSE를 "전체 세션"에 보낼 때 사용.
    oss << "a=control:*\r\n";

    // m= : 미디어 라인. 한 미디어 종류당 한 줄.
    //   - "video" : 미디어 타입
    //   - "0"     : 포트. RTSP에서는 SETUP에서 결정하므로 0 (placeholder).
    //   - "RTP/AVP": 전송 프로토콜 (RTP Audio/Video Profile = RTP over UDP)
    //   - payload type 번호 (한 줄에 여러 PT 나열 가능)
    oss << "m=video 0 RTP/AVP " << info.payload_type << "\r\n";

    // a=rtpmap : 동적 PT를 코덱과 클럭에 매핑.
    //   "PT 코덱이름/클럭레이트[/채널수]"
    //   H.264는 90000 Hz가 관습 (NTSC/PAL 양쪽에서 깔끔히 나뉘는 수).
    oss << "a=rtpmap:" << info.payload_type << " H264/" << info.clock_rate << "\r\n";

    // a=fmtp : 코덱별 포맷 파라미터.
    //   packetization-mode=1 : Single NAL + STAP-A + FU-A 모드 (FU-A 사용 가능).
    //   packetization-mode=0 : Single NAL만 (1400 넘는 NAL 전송 불가).
    //   sprop-parameter-sets : SPS/PPS Base64. 채워두면 디코더가 첫 키프레임을
    //                          기다리지 않고 미리 준비 가능.
    oss << "a=fmtp:" << info.payload_type << " packetization-mode=1";
    if (!info.sprop_parameter_sets.empty()) {
        oss << ";sprop-parameter-sets=" << info.sprop_parameter_sets;
    }
    oss << "\r\n";

    // a=control:trackN (미디어 레벨)
    //   이 트랙의 개별 제어 URL. 클라이언트는 DESCRIBE 응답의 Content-Base와
    //   합쳐서 "<base>/track0" 같은 URL을 만들어 SETUP에 보낸다.
    oss << "a=control:track0\r\n";

    return oss.str();
}

}  // namespace veda::sdp

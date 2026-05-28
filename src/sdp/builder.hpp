// src/sdp/builder.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// SDP (Session Description Protocol) 본문 생성기.
// DESCRIBE 응답의 본문으로 들어간다. 참고: RFC 4566 (SDP), RFC 6184 (H.264).
// ─────────────────────────────────────────────────────────────────────────────
//
// [SDP는 어떤 모양인가]
//   줄마다 "<글자>=<값>" 형식. 줄 순서가 의미를 가진다.
//
//   v=0                              ← 프로토콜 버전 (항상 0)
//   o=- 0 0 IN IP4 0.0.0.0           ← origin: 세션 작성자 정보
//   s=VEDA RTSP Stream               ← session name (사람이 읽는 라벨)
//   c=IN IP4 0.0.0.0                 ← connection info (네트워크 주소)
//   t=0 0                            ← timing (0 0 = 영구 세션)
//   m=video 0 RTP/AVP 96             ← media: 비디오, port=0(SETUP에서 결정),
//                                            전송=RTP/AVP, payload type=96
//   a=rtpmap:96 H264/90000           ← PT 96은 H.264, 90kHz 클럭
//   a=fmtp:96 packetization-mode=1   ← H.264 옵션 (FU-A 분할 모드)
//                                     ;sprop-parameter-sets=<SPS,PPS base64>
//   a=control:track0                 ← 트랙 제어 URL
//
// [핵심 포인트]
//   1) "m=" 줄은 미디어 한 종류씩 한 줄. video, audio 둘 다면 m=가 두 개.
//   2) "a=" 줄은 세션 또는 그 직전 m=에 종속된 속성. 위치가 의미를 가진다.
//   3) "rtpmap:N codec/rate": 동적 payload type N이 어떤 코덱+클럭인지 정의.
//   4) "fmtp:N options"     : 그 코덱 고유의 옵션. H.264면 packetization-mode,
//                              profile-level-id, sprop-parameter-sets 등.
//
// [페이로드 타입(PT) 96을 쓰는 이유]
//   0~95는 RTP 표준 코덱이 예약되어 있고, 96~127은 동적 영역.
//   H.264는 표준 PT가 없어서 동적 영역에서 골라 쓰고 rtpmap으로 매핑한다.

#pragma once

#include <string>

namespace veda::sdp {

struct VideoStreamInfo {
    int payload_type = 96;       // 96~127 중 하나 (관습적으로 96)
    int clock_rate   = 90000;    // 비디오 RTP의 관습 클럭 (90 kHz)

    // SPS/PPS를 Base64로 인코딩한 문자열. 형식: "<SPS_b64>,<PPS_b64>"
    // 비어 있으면 fmtp 줄에서 sprop-parameter-sets 항목을 빼서 작성한다.
    // 채워두면 클라이언트가 스트림 시작 전에 디코더를 미리 준비할 수 있어
    // 첫 프레임 표시까지의 지연이 줄어든다.
    std::string sprop_parameter_sets;
};

// SDP 본문을 만들어 문자열로 반환.
// 반환값은 그대로 DESCRIBE 응답의 본문에 들어간다 (Content-Length도 이걸 기준).
std::string build(const VideoStreamInfo& info);

}  // namespace veda::sdp

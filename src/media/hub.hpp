// src/media/hub.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// 미디어 허브 — 소스 한 개의 NAL을 여러 RTP 스트리머에 동시 분배(fan-out)한다.
// ─────────────────────────────────────────────────────────────────────────────
//
// [왜 필요한가 — 멀티클라이언트의 핵심 문제]
//   카메라는 물리적으로 한 대뿐이다. libcamera-vid를 클라이언트마다 띄우면
//   두 번째 프로세스부터 "camera busy"로 실패한다. 그래서 구조를 뒤집는다:
//
//     (기존)  PLAY 핸들러 안에서 카메라를 열고 그 자리에서 송출 루프
//             → 루프 도는 동안 accept 불가 = 싱글 클라이언트
//
//     (현재)  Hub가 소스를 단 하나 소유하고 전용 스레드에서 NAL을 펌핑.
//             PLAY는 자기 Streamer를 Hub에 "구독"만 시키고 즉시 반환
//             → epoll 루프는 계속 돌고, 클라이언트 N명이 같은 영상을 받음
//
// [스레드 모델]
//   - epoll 스레드  : subscribe/unsubscribe/sprop 호출 (RTSP 처리 중)
//   - 펌프 스레드   : 소스에서 read_nal → 구독자 전원에게 send_nal
//   둘이 만나는 지점(구독자 목록, SPS/PPS 캐시)은 mu_ 하나로 보호한다.
//   unsubscribe가 mu_를 잡으면 진행 중인 브로드캐스트가 끝날 때까지
//   대기하므로, 해제된 Streamer를 펌프 스레드가 만질 일이 없다.
//
// [수명 관리]
//   첫 구독자가 들어오면 소스를 켜고 펌프 스레드를 시작, 마지막 구독자가
//   나가면 정지한다 — 아무도 안 보는데 카메라를 돌릴 이유가 없다.
//
// [SPS/PPS 캐시]
//   스트림에서 type 7(SPS)/8(PPS) NAL을 발견하면 저장해 둔다.
//   DESCRIBE가 sprop-parameter-sets를 요구할 때 Base64로 인코딩해 제공.
//   파일 소스는 시작 전에 미리 스캔(prescan)하므로 첫 DESCRIBE부터 채워진다.

#pragma once

#include "media/nal_source.hpp"
#include "rtp/streamer.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace veda::media {

// 소스 선택 설정. source가 "camera"면 libcamera-vid, 아니면 .h264 파일 경로.
struct SourceConfig {
    std::string source = "camera";
    int width  = 640;
    int height = 480;
    int fps    = 30;
};

class Hub {
public:
    explicit Hub(SourceConfig cfg);
    ~Hub();

    Hub(const Hub&)            = delete;
    Hub& operator=(const Hub&) = delete;

    // 스트리머를 분배 대상에 추가. 첫 구독자면 소스+펌프 스레드 가동.
    // 반환값: false = 소스를 켜지 못함 (카메라 없음 / 파일 없음)
    bool subscribe(std::shared_ptr<rtp::Streamer> streamer);

    // 분배 대상에서 제거. 마지막 구독자면 소스 정지 + 스레드 join.
    // 반환 후에는 펌프 스레드가 해당 스트리머를 절대 만지지 않음이 보장된다.
    void unsubscribe(const std::shared_ptr<rtp::Streamer>& streamer);

    // SDP용 "sprop-parameter-sets" 값: "<SPS_b64>,<PPS_b64>".
    // 아직 SPS/PPS를 못 봤으면 빈 문자열 (카메라 모드는 --inline이라 무방).
    std::string sprop_parameter_sets();

    int fps() const { return cfg_.fps; }

private:
    std::unique_ptr<NalSource> make_source() const;
    void pump();                                        // 펌프 스레드 본체
    void on_nal(const uint8_t* nal, std::size_t len);   // NAL 1개 분배
    void prescan_parameter_sets();                      // 파일 모드 전용 사전 스캔

    SourceConfig cfg_;

    std::mutex mu_;  // subscribers_, sps_, pps_ 보호
    std::vector<std::shared_ptr<rtp::Streamer>> subscribers_;

    std::unique_ptr<NalSource> source_;
    std::thread pump_thread_;
    std::atomic<bool> running_{false};

    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;

    // 프레임당 RTP timestamp 증가량 = 90000 / fps (30fps → 3000)
    uint32_t ts_increment_;
};

}  // namespace veda::media

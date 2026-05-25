// src/rtp/streamer.hpp
//
// RTP Streamer: H.264 NAL units를 RTP 패킷으로 전송

#pragma once

#include "net/udp_socket.hpp"
#include "rtp/packetizer.hpp"
#include "rtp/h264_fua.hpp"

#include <cstdint>
#include <string>
#include <atomic>
#include <thread>
#include <memory>

namespace veda::rtp {

class Streamer {
public:
    Streamer();
    ~Streamer();

    Streamer(const Streamer&) = delete;
    Streamer& operator=(const Streamer&) = delete;

    // 클라이언트 연결 설정
    bool connect(const std::string& client_ip, uint16_t client_rtp_port);

    // NAL unit 전송 (자동으로 FU-A 분할)
    bool send_nal(const uint8_t* nal, std::size_t nal_len);

    // 현재 timestamp 증가 (H.264 @ 90kHz, 30fps 기준: 3000씩 증가)
    void advance_timestamp(uint32_t increment = 3000);

    // SSRC 설정/조회
    void set_ssrc(uint32_t ssrc) { ssrc_ = ssrc; }
    uint32_t ssrc() const { return ssrc_; }

    // 상태 조회
    bool is_connected() const { return connected_; }
    uint16_t sequence() const { return sequence_; }
    uint32_t timestamp() const { return timestamp_; }

private:
    net::UdpSocket socket_;
    bool connected_ = false;

    // RTP 상태
    uint16_t sequence_ = 0;    // 시퀀스 번호 (0부터 시작, 각 패킷마다 +1)
    uint32_t timestamp_ = 0;   // 타임스탬프 (같은 NAL 내에서는 동일)
    uint32_t ssrc_ = 0x12345678;  // 동기화 소스 ID (랜덤 고정값)
    uint8_t payload_type_ = 96;   // H.264 동적 페이로드 타입
};

}  // namespace veda::rtp

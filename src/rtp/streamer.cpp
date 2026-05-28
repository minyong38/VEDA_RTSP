// src/rtp/streamer.cpp
//
// "NAL을 RTP 패킷 시퀀스로 변환해서 UDP로 뿌리는" 한 줄짜리 책임.
// 이 클래스가 packetizer + h264_fua + udp_socket을 묶어주는 접착제.

#include "rtp/streamer.hpp"

#include <iostream>
#include <random>

namespace veda::rtp {

Streamer::Streamer() {
    // ─── 보안 권장사항 (RFC 3550 §5.1) ─────────────────────────────────────
    //
    // SSRC와 초기 시퀀스를 랜덤으로 잡는 이유:
    //   - SSRC: 같은 세션 내 다른 송신자와 우연히 겹치면 안 됨
    //   - sequence: 0부터 시작하면 공격자가 끼어들기 쉬움 (예측 가능 ⇒ 스푸핑)
    //
    // mt19937: Mersenne Twister, 통계적으로 잘 퍼지는 PRNG.
    // random_device: 시드용 엔트로피 소스 (보통 OS의 /dev/urandom 등).
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
    ssrc_ = dis(gen);

    std::uniform_int_distribution<uint16_t> seq_dis(0, 0xFFFF);
    sequence_ = seq_dis(gen);
}

Streamer::~Streamer() = default;

bool Streamer::connect(const std::string& client_ip, uint16_t client_rtp_port) {
    // UDP 소켓에 "기본 목적지"를 박는다. 이후 send()만으로 전송 가능.
    if (!socket_.connect_to(client_ip, client_rtp_port)) {
        std::cerr << "[RTP Streamer] Failed to connect to "
                  << client_ip << ":" << client_rtp_port << "\n";
        return false;
    }

    connected_ = true;
    std::cout << "[RTP Streamer] Connected to " << client_ip << ":" << client_rtp_port
              << " (SSRC: 0x" << std::hex << ssrc_ << std::dec << ")\n";
    return true;
}

bool Streamer::send_nal(const uint8_t* nal, std::size_t nal_len) {
    // 방어 코드: 미연결 또는 빈 입력이면 즉시 false.
    if (!connected_ || nal == nullptr || nal_len == 0) {
        return false;
    }

    // ─── 1단계: NAL을 RTP 페이로드 묶음으로 자른다 ─────────────────────────
    // nal_len ≤ 1400이면 1개 묶음, 그 이상이면 FU-A 여러 묶음.
    auto fragments = fragment_nal(nal, nal_len);

    if (fragments.empty()) {
        return false;
    }

    // ─── 2단계: 묶음마다 RTP 패킷을 만들고 UDP로 쏜다 ──────────────────────
    for (std::size_t i = 0; i < fragments.size(); ++i) {
        const auto& payload = fragments[i];
        bool is_last = (i == fragments.size() - 1);

        // RTP 헤더 채우기.
        //   - sequence: 매 패킷 +1 (uint16 자연 wrap 사용)
        //   - timestamp: 한 NAL 내내 동일. 프레임 단위 advance는 호출 측에서.
        //   - marker: NAL의 마지막 조각에만 1.
        //     (엄밀히 RTP marker는 "프레임의 마지막 패킷"이지만, 현재
        //      구조에서는 한 NAL이 한 프레임이라고 가정. SPS/PPS 같은
        //      non-VCL NAL이 끼면 marker가 어긋날 수 있다 — 향후 개선 포인트.)
        Header hdr;
        hdr.payload_type = payload_type_;
        hdr.sequence     = sequence_++;
        hdr.timestamp    = timestamp_;
        hdr.ssrc         = ssrc_;
        hdr.marker       = is_last;

        // 헤더 12B + 페이로드 = 완전한 RTP 패킷.
        auto packet = build_packet(hdr, payload.data(), payload.size());

        // UDP 한 번에 한 데이터그램. 부분 전송 처리 필요 없음.
        ssize_t sent = socket_.send(packet.data(), packet.size());
        if (sent < 0) {
            std::cerr << "[RTP Streamer] Failed to send packet\n";
            return false;
        }
    }

    return true;
}

void Streamer::advance_timestamp(uint32_t increment) {
    // 한 프레임이 끝났을 때만 호출해야 한다.
    // increment 기본 3000 = 90000 Hz / 30 fps. 다른 fps면 호출 측에서 계산.
    timestamp_ += increment;
}

}  // namespace veda::rtp

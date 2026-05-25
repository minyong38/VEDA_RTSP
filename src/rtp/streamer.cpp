// src/rtp/streamer.cpp

#include "rtp/streamer.hpp"

#include <iostream>
#include <random>

namespace veda::rtp {

Streamer::Streamer() {
    // 랜덤 SSRC 생성
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
    ssrc_ = dis(gen);

    // 랜덤 초기 시퀀스 번호 (보안 권장사항)
    std::uniform_int_distribution<uint16_t> seq_dis(0, 0xFFFF);
    sequence_ = seq_dis(gen);
}

Streamer::~Streamer() = default;

bool Streamer::connect(const std::string& client_ip, uint16_t client_rtp_port) {
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
    if (!connected_ || nal == nullptr || nal_len == 0) {
        return false;
    }

    // NAL을 FU-A로 분할 (MTU 초과 시)
    auto fragments = fragment_nal(nal, nal_len);

    if (fragments.empty()) {
        return false;
    }

    // 각 fragment를 RTP 패킷으로 전송
    for (std::size_t i = 0; i < fragments.size(); ++i) {
        const auto& payload = fragments[i];
        bool is_last = (i == fragments.size() - 1);

        // RTP 헤더 구성
        Header hdr;
        hdr.payload_type = payload_type_;
        hdr.sequence = sequence_++;
        hdr.timestamp = timestamp_;  // 같은 NAL 내에서는 동일한 timestamp
        hdr.ssrc = ssrc_;
        hdr.marker = is_last;  // 마지막 패킷에만 marker bit 설정

        // RTP 패킷 생성 및 전송
        auto packet = build_packet(hdr, payload.data(), payload.size());

        ssize_t sent = socket_.send(packet.data(), packet.size());
        if (sent < 0) {
            std::cerr << "[RTP Streamer] Failed to send packet\n";
            return false;
        }
    }

    return true;
}

void Streamer::advance_timestamp(uint32_t increment) {
    timestamp_ += increment;
}

}  // namespace veda::rtp

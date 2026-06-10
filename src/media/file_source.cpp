// src/media/file_source.cpp
//
// .h264 파일을 30fps 페이싱으로 끝없이 재생하는 NalSource 구현.

#include "media/file_source.hpp"

#include <iostream>
#include <thread>

namespace veda::media {

FileSource::FileSource(std::string path, int fps, bool loop)
    : path_(std::move(path)), fps_(fps > 0 ? fps : 30), loop_(loop) {}

FileSource::~FileSource() {
    stop();
}

bool FileSource::start() {
    if (file_) {
        return true;  // 이미 열려 있음
    }

    // "rb": 바이너리 모드. 텍스트 모드로 열면 일부 플랫폼에서 0x1A 등을
    // EOF로 오해하는 사고가 난다.
    file_ = std::fopen(path_.c_str(), "rb");
    if (!file_) {
        std::cerr << "[FileSource] Failed to open: " << path_ << "\n";
        return false;
    }

    reader_.reset();
    next_frame_time_ = std::chrono::steady_clock::now();
    std::cout << "[FileSource] Opened " << path_ << " (" << fps_ << " fps"
              << (loop_ ? ", loop" : "") << ")\n";
    return true;
}

void FileSource::stop() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
        reader_.reset();
        std::cout << "[FileSource] Stopped\n";
    }
}

bool FileSource::read_nal(const NalCallback& callback) {
    if (!file_) {
        return false;
    }

    // 콜백을 감싸서 "이번 호출에서 VCL NAL이 나왔는지"를 기록한다.
    // VCL NAL(type 1=P 슬라이스, 5=IDR 키프레임)이 곧 프레임 한 장이므로,
    // 그때만 1/fps 만큼 페이싱한다. SPS/PPS/SEI 같은 non-VCL NAL은
    // 다음 프레임에 붙는 메타데이터라 지체 없이 내보낸다.
    bool frame_boundary = false;
    auto wrapped = [&](const uint8_t* nal, std::size_t len) {
        if (len > 0) {
            uint8_t nal_type = nal[0] & 0x1F;
            if (nal_type == 1 || nal_type == 5) {
                frame_boundary = true;
            }
        }
        callback(nal, len);
    };

    if (!reader_.read_nal(file_, wrapped)) {
        // EOF 도달. 루프 모드면 처음으로 되감고 한 번 더 시도한다.
        // (EOF 직전에 잔여 NAL이 콜백됐을 수 있지만, 어차피 다음 호출
        //  흐름과 동일하므로 여기서는 되감기만 처리하면 된다.)
        if (!loop_) {
            return false;
        }
        std::rewind(file_);
        reader_.reset();
        if (!reader_.read_nal(file_, wrapped)) {
            // 되감았는데도 못 읽으면 파일 자체가 비었거나 깨진 것.
            std::cerr << "[FileSource] No NAL found after rewind: " << path_ << "\n";
            return false;
        }
    }

    // ─── 실시간 페이싱 ─────────────────────────────────────────────────────
    // 프레임 한 장을 내보냈으면 다음 프레임 시각까지 잔다. sleep 대신
    // sleep_until을 쓰는 이유: "처리 시간 + 33ms"로 늘어지는 누적 드리프트를
    // 막고 장기적으로 정확히 fps를 유지하기 위해.
    if (frame_boundary) {
        next_frame_time_ += std::chrono::microseconds(1000000 / fps_);
        std::this_thread::sleep_until(next_frame_time_);
    }

    return true;
}

}  // namespace veda::media

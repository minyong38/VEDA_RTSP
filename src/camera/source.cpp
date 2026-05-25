// src/camera/source.cpp

#include "camera/source.hpp"

#include <iostream>
#include <sstream>
#include <cstring>

namespace veda::camera {

Source::Source(int width, int height, int fps)
    : width_(width), height_(height), fps_(fps) {
    buffer_.reserve(kReadChunkSize * 4);
}

Source::~Source() {
    stop();
}

bool Source::start() {
    if (pipe_) {
        return true;  // 이미 실행 중
    }

    // libcamera-vid 명령어 구성
    // -t 0: 무한 실행
    // --inline: SPS/PPS를 스트림에 포함
    // -o -: stdout으로 출력
    std::ostringstream cmd;
    cmd << "libcamera-vid"
        << " -t 0"
        << " --width " << width_
        << " --height " << height_
        << " --framerate " << fps_
        << " --codec h264"
        << " --inline"
        << " -o -"
        << " 2>/dev/null";  // stderr 무시

    std::cout << "[Camera] Starting: " << cmd.str() << "\n";

    pipe_ = popen(cmd.str().c_str(), "r");
    if (!pipe_) {
        std::cerr << "[Camera] Failed to start libcamera-vid\n";
        return false;
    }

    std::cout << "[Camera] Started successfully\n";
    return true;
}

void Source::stop() {
    if (pipe_) {
        pclose(pipe_);
        pipe_ = nullptr;
        buffer_.clear();
        std::cout << "[Camera] Stopped\n";
    }
}

bool Source::find_start_code(std::size_t& pos, std::size_t& start_code_len) {
    // H.264 start code: 0x00 0x00 0x01 (3바이트) 또는 0x00 0x00 0x00 0x01 (4바이트)
    for (std::size_t i = 0; i + 2 < buffer_.size(); ++i) {
        if (buffer_[i] == 0x00 && buffer_[i + 1] == 0x00) {
            if (buffer_[i + 2] == 0x01) {
                pos = i;
                start_code_len = 3;
                return true;
            }
            if (i + 3 < buffer_.size() && buffer_[i + 2] == 0x00 && buffer_[i + 3] == 0x01) {
                pos = i;
                start_code_len = 4;
                return true;
            }
        }
    }
    return false;
}

bool Source::read_nal(NalCallback callback) {
    if (!pipe_) {
        return false;
    }

    // 버퍼에 데이터 읽기
    uint8_t chunk[kReadChunkSize];

    while (true) {
        // 첫 번째 start code 찾기
        std::size_t first_pos = 0, first_len = 0;
        if (!find_start_code(first_pos, first_len)) {
            // start code가 없으면 더 읽기
            std::size_t n = fread(chunk, 1, kReadChunkSize, pipe_);
            if (n == 0) {
                if (feof(pipe_)) {
                    return false;  // EOF
                }
                continue;
            }
            buffer_.insert(buffer_.end(), chunk, chunk + n);
            continue;
        }

        // 두 번째 start code 찾기 (NAL 끝 찾기)
        std::size_t nal_start = first_pos + first_len;
        std::size_t search_offset = nal_start;

        while (true) {
            // search_offset부터 다음 start code 찾기
            bool found = false;
            for (std::size_t i = search_offset; i + 2 < buffer_.size(); ++i) {
                if (buffer_[i] == 0x00 && buffer_[i + 1] == 0x00) {
                    if (buffer_[i + 2] == 0x01 ||
                        (i + 3 < buffer_.size() && buffer_[i + 2] == 0x00 && buffer_[i + 3] == 0x01)) {
                        // NAL unit 발견 (start_pos ~ i)
                        std::size_t nal_len = i - nal_start;
                        if (nal_len > 0) {
                            callback(buffer_.data() + nal_start, nal_len);
                        }
                        // 버퍼에서 처리된 부분 제거
                        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(i));
                        return true;
                    }
                }
            }

            if (!found) {
                // 다음 start code가 없으면 더 읽기
                std::size_t n = fread(chunk, 1, kReadChunkSize, pipe_);
                if (n == 0) {
                    if (feof(pipe_)) {
                        // EOF: 남은 버퍼가 마지막 NAL
                        if (buffer_.size() > nal_start) {
                            callback(buffer_.data() + nal_start, buffer_.size() - nal_start);
                            buffer_.clear();
                        }
                        return false;
                    }
                    continue;
                }
                buffer_.insert(buffer_.end(), chunk, chunk + n);
                search_offset = buffer_.size() - n - 3;  // 새로 읽은 부분부터 검색
                if (search_offset < nal_start) {
                    search_offset = nal_start;
                }
            }
        }
    }
}

}  // namespace veda::camera

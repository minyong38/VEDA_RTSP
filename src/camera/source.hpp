// src/camera/source.hpp
//
// libcamera-vid를 사용한 H.264 카메라 소스

#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include <functional>

namespace veda::camera {

// NAL unit 콜백: NAL 데이터와 길이를 받음
using NalCallback = std::function<void(const uint8_t* nal, std::size_t len)>;

class Source {
public:
    Source(int width = 640, int height = 480, int fps = 30);
    ~Source();

    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    // 카메라 시작 (libcamera-vid 프로세스 실행)
    bool start();

    // 카메라 중지
    void stop();

    // NAL unit 읽기 (블로킹, 하나의 NAL을 읽어서 콜백 호출)
    // 반환값: true = 성공, false = EOF 또는 에러
    bool read_nal(NalCallback callback);

    // 상태
    bool is_running() const { return pipe_ != nullptr; }

private:
    // H.264 스트림에서 NAL unit 경계 찾기
    // start code: 0x00 0x00 0x00 0x01 또는 0x00 0x00 0x01
    bool find_start_code(std::size_t& pos, std::size_t& start_code_len);

    int width_;
    int height_;
    int fps_;
    FILE* pipe_ = nullptr;

    // 버퍼 (NAL 파싱용)
    std::vector<uint8_t> buffer_;
    static constexpr std::size_t kReadChunkSize = 4096;
};

}  // namespace veda::camera

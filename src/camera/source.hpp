// src/camera/source.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// 라즈베리파이 카메라 → H.264 NAL 스트림 소스.
// ─────────────────────────────────────────────────────────────────────────────
//
// [동작 원리]
//   `libcamera-vid` 명령어를 popen()으로 띄우고 stdout을 파이프로 읽는다.
//   libcamera-vid가 토하는 건 raw H.264 Annex-B 바이트 스트림이고,
//   우리는 그 안에서 start code(0x000001 또는 0x00000001)를 찾아 NAL 경계를
//   그어 한 개씩 끄집어낸다.
//
// [왜 libcamera C++ API 안 쓰고 popen?]
//   - 학습 목적: RTSP/RTP 프로토콜 직접 구현이 본 주제. 카메라 드라이버까지
//     직접 만들 필요는 없음.
//   - 외부 의존성 최소화: libcamera++ 헤더를 빌드에 끌고 오지 않아도 됨.
//   - 디버그 편의: 쉘에서 같은 명령으로 카메라 동작 확인 가능.
//
// [start code 두 가지]
//   - 3-byte: 0x00 0x00 0x01           (대부분의 NAL)
//   - 4-byte: 0x00 0x00 0x00 0x01      (스트림 시작/SPS·PPS 앞 등)
//   Annex-B 포맷에서는 NAL과 NAL 사이에 둘 중 하나가 들어간다.

#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include <functional>

namespace veda::camera {

// NAL 한 개가 준비되면 호출되는 콜백.
//   nal : NAL 헤더 바이트부터 시작 (start code는 떼고 들어옴)
//   len : NAL 전체 길이 (헤더 포함)
using NalCallback = std::function<void(const uint8_t* nal, std::size_t len)>;

class Source {
public:
    // 기본값은 640x480 @ 30fps. 라파4에서 충분히 처리 가능.
    Source(int width = 640, int height = 480, int fps = 30);
    ~Source();

    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    // libcamera-vid 프로세스 시작. 이미 켜져 있으면 그대로 true.
    bool start();

    // 프로세스 종료 (pclose). 자동으로 소멸자에서도 호출됨.
    void stop();

    // 다음 NAL이 도착할 때까지 블로킹 → 콜백 호출 후 반환.
    // 반환값: true=정상, false=EOF나 에러 (스트림 종료)
    bool read_nal(NalCallback callback);

    bool is_running() const { return pipe_ != nullptr; }

private:
    // 내부 버퍼에서 start code를 찾아 위치/길이를 알려준다.
    //   pos             : start code의 시작 인덱스
    //   start_code_len  : 3 또는 4
    // 못 찾으면 false.
    bool find_start_code(std::size_t& pos, std::size_t& start_code_len);

    int width_;
    int height_;
    int fps_;
    FILE* pipe_ = nullptr;   // popen()의 결과

    // NAL 파싱용 누적 버퍼. fread가 청크 단위로 채우고, find_start_code가
    // 그 안에서 NAL 경계를 그어 콜백으로 토해낸 뒤 erase로 앞을 잘라낸다.
    std::vector<uint8_t> buffer_;
    static constexpr std::size_t kReadChunkSize = 4096;
};

}  // namespace veda::camera

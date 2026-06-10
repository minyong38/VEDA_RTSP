// src/media/nal_source.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// "H.264 NAL을 한 개씩 내놓는 것"의 추상 인터페이스.
// ─────────────────────────────────────────────────────────────────────────────
//
// [왜 인터페이스를 두나]
//   NAL의 출처는 두 가지가 있다:
//     1) camera::Source  — 라즈베리파이 카메라 (libcamera-vid popen)
//     2) media::FileSource — 미리 인코딩된 .h264 파일 (개발 PC 테스트용)
//   소비자(media::Hub)는 출처가 무엇이든 "start → read_nal 반복 → stop"
//   계약만 알면 되므로, 그 계약을 가상 함수로 고정한다.
//
//   덕분에 카메라가 없는 환경에서도 `--source test.h264`로 서버 전체를
//   끝까지(VLC 재생까지) 검증할 수 있다.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace veda::media {

// NAL 한 개가 준비되면 호출되는 콜백.
//   nal : NAL 헤더 바이트부터 시작 (Annex-B start code는 떼고 들어옴)
//   len : NAL 전체 길이 (헤더 포함)
using NalCallback = std::function<void(const uint8_t* nal, std::size_t len)>;

class NalSource {
public:
    virtual ~NalSource() = default;

    // 소스 가동 (프로세스/파일 열기). 실패 시 false.
    virtual bool start() = 0;

    // 소스 정지 + 자원 해제. start 전이나 중복 호출도 안전해야 한다.
    virtual void stop() = 0;

    // 다음 NAL 한 개가 준비될 때까지 블로킹 → 콜백 호출 후 반환.
    // 반환값: true=정상, false=EOF나 에러 (스트림 종료)
    virtual bool read_nal(const NalCallback& callback) = 0;
};

}  // namespace veda::media

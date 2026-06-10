// src/camera/source.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// 라즈베리파이 카메라 → H.264 NAL 스트림 소스.
// ─────────────────────────────────────────────────────────────────────────────
//
// [동작 원리]
//   `libcamera-vid` 명령어를 popen()으로 띄우고 stdout을 파이프로 읽는다.
//   libcamera-vid가 토하는 건 raw H.264 Annex-B 바이트 스트림이고,
//   NAL 경계 분리는 media::AnnexBReader가 담당한다 (FileSource와 공유).
//
// [왜 libcamera C++ API 안 쓰고 popen?]
//   - 학습 목적: RTSP/RTP 프로토콜 직접 구현이 본 주제. 카메라 드라이버까지
//     직접 만들 필요는 없음.
//   - 외부 의존성 최소화: libcamera++ 헤더를 빌드에 끌고 오지 않아도 됨.
//   - 디버그 편의: 쉘에서 같은 명령으로 카메라 동작 확인 가능.
//
// [media::NalSource 구현]
//   소비자(media::Hub)는 카메라인지 파일인지 모른 채 start/read_nal/stop
//   계약만으로 NAL을 받아간다.

#pragma once

#include "media/annexb_reader.hpp"
#include "media/nal_source.hpp"

#include <cstdio>

namespace veda::camera {

// 기존 호출부 호환용 별칭. 시그니처는 media::NalCallback과 동일하다.
using NalCallback = media::NalCallback;

class Source : public media::NalSource {
public:
    // 기본값은 640x480 @ 30fps. 라파4에서 충분히 처리 가능.
    explicit Source(int width = 640, int height = 480, int fps = 30);
    ~Source() override;

    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    // libcamera-vid 프로세스 시작. 이미 켜져 있으면 그대로 true.
    bool start() override;

    // 프로세스 종료 (pclose). 자동으로 소멸자에서도 호출됨.
    void stop() override;

    // 다음 NAL이 도착할 때까지 블로킹 → 콜백 호출 후 반환.
    // 반환값: true=정상, false=EOF나 에러 (스트림 종료)
    bool read_nal(const NalCallback& callback) override;

    bool is_running() const { return pipe_ != nullptr; }

private:
    int width_;
    int height_;
    int fps_;
    FILE* pipe_ = nullptr;   // popen()의 결과

    // Annex-B 스트림에서 NAL 경계를 긋는 공용 헬퍼 (start code 탐색 + 누적 버퍼)
    media::AnnexBReader reader_;
};

}  // namespace veda::camera

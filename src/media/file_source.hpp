// src/media/file_source.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// 미리 인코딩된 .h264 (Annex-B) 파일을 "실시간 카메라처럼" 재생하는 소스.
// ─────────────────────────────────────────────────────────────────────────────
//
// [용도]
//   라즈베리파이 카메라가 없는 개발 PC에서 서버 전체 경로(RTSP 핸드셰이크 →
//   RTP 송출 → VLC 재생)를 검증하기 위한 소스. 테스트 파일은 ffmpeg로 생성:
//
//     ffmpeg -f lavfi -i testsrc=duration=60:size=640x480:rate=30
//            -c:v libx264 -preset ultrafast -tune zerolatency
//            -x264-params keyint=30 -f h264 tools/samples/test.h264
//
// [왜 페이싱(pacing)이 필요한가]
//   카메라는 30fps로 "천천히" NAL을 내놓지만 파일은 디스크 속도로 읽힌다.
//   그대로 쏘면 60초짜리 영상이 1초 만에 클라이언트에 쏟아져 버퍼가 터지고
//   재생이 망가진다. 그래서 프레임 경계(VCL NAL)마다 1/fps 만큼 쉬어서
//   실시간 속도를 흉내 낸다.
//
// [루프 재생]
//   EOF에 닿으면 rewind해서 처음부터 다시 — 라이브 스트림처럼 끝없이 재생.
//   (RTP timestamp는 계속 증가하므로 클라이언트 입장에선 이어지는 영상)

#pragma once

#include "media/annexb_reader.hpp"
#include "media/nal_source.hpp"

#include <chrono>
#include <string>

namespace veda::media {

class FileSource : public NalSource {
public:
    explicit FileSource(std::string path, int fps = 30, bool loop = true);
    ~FileSource() override;

    FileSource(const FileSource&)            = delete;
    FileSource& operator=(const FileSource&) = delete;

    bool start() override;   // fopen. 파일 없으면 false.
    void stop() override;    // fclose. 중복 호출 안전.
    bool read_nal(const NalCallback& callback) override;

private:
    std::string path_;
    int         fps_;
    bool        loop_;
    FILE*       file_ = nullptr;
    AnnexBReader reader_;

    // 다음 프레임을 내보낼 시각. VCL NAL(type 1/5)을 내보낼 때마다
    // 1/fps 씩 뒤로 민다. sleep_until 기반이라 누적 오차가 없다.
    std::chrono::steady_clock::time_point next_frame_time_;
};

}  // namespace veda::media

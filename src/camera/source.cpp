// src/camera/source.cpp
//
// ─────────────────────────────────────────────────────────────────────────────
// 카메라(libcamera-vid)에서 H.264 NAL을 끄집어내는 어댑터.
// ─────────────────────────────────────────────────────────────────────────────
//
// [데이터 흐름]
//
//   libcamera-vid stdout
//          │
//          ▼  (popen)
//   media::AnnexBReader ─── start code 기준 NAL 분리 ───→ callback
//
// NAL 경계 분리 로직은 media/annexb_reader.cpp로 이동했다 (FileSource와 공유).
// 이 파일에 남은 책임은 libcamera-vid 프로세스의 수명 관리뿐이다.

#include "camera/source.hpp"

#include <iostream>
#include <sstream>

namespace veda::camera {

Source::Source(int width, int height, int fps)
    : width_(width), height_(height), fps_(fps) {}

Source::~Source() {
    stop();
}

bool Source::start() {
    if (pipe_) {
        return true;  // 이미 실행 중
    }

    // ─── libcamera-vid 명령어 구성 ──────────────────────────────────────────
    //
    //   -t 0           : 무한 실행 (기본은 5초 후 종료)
    //   --width/--height/--framerate : 해상도와 프레임레이트
    //   --codec h264   : 출력 코덱
    //   --inline       : SPS/PPS를 스트림 안에 주기적으로 끼워넣음
    //                    (DESCRIBE 응답에 sprop-parameter-sets를 못 채워도
    //                     클라이언트가 키프레임 직전에 SPS/PPS를 만나서
    //                     디코딩 시작 가능)
    //   -o -           : stdout으로 출력
    //   2>/dev/null    : stderr 무시 (libcamera 자체 진단 메시지 차단)
    std::ostringstream cmd;
    cmd << "libcamera-vid"
        << " -t 0"
        << " --width "     << width_
        << " --height "    << height_
        << " --framerate " << fps_
        << " --codec h264"
        << " --inline"
        << " -o -"
        << " 2>/dev/null";

    std::cout << "[Camera] Starting: " << cmd.str() << "\n";

    // popen은 fork+exec+pipe를 한 호출로 묶은 편의 함수.
    // "r" 모드 = 자식의 stdout을 우리가 읽는다.
    pipe_ = popen(cmd.str().c_str(), "r");
    if (!pipe_) {
        std::cerr << "[Camera] Failed to start libcamera-vid\n";
        return false;
    }

    reader_.reset();
    std::cout << "[Camera] Started successfully\n";
    return true;
}

void Source::stop() {
    if (pipe_) {
        // pclose는 자식 프로세스의 종료를 기다린다 (SIGCHLD 처리도 같이).
        pclose(pipe_);
        pipe_ = nullptr;
        reader_.reset();
        std::cout << "[Camera] Stopped\n";
    }
}

bool Source::read_nal(const NalCallback& callback) {
    if (!pipe_) {
        return false;
    }
    return reader_.read_nal(pipe_, callback);
}

}  // namespace veda::camera

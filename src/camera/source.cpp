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
//          ▼  (popen + fread, 4KB 청크씩)
//   buffer_  ─── start code 찾기 ─── 두 start code 사이가 NAL 한 개 ───→ callback
//          │
//          └─ erase로 앞부분 잘라 다음 NAL 준비
//
// [알려진 한계 (앞 대화에서 언급한 버그)]
//   현재 코드의 search_offset 갱신 로직과 `found` 플래그 사용에 결함이 있어,
//   특정 상황에서 무한 read 루프나 underflow가 발생할 수 있다.
//   동작은 하지만 견고하지 않으므로 다음 학습 항목으로 다시 손볼 자리.

#include "camera/source.hpp"

#include <iostream>
#include <sstream>
#include <cstring>

namespace veda::camera {

Source::Source(int width, int height, int fps)
    : width_(width), height_(height), fps_(fps) {
    // 미리 4 청크 정도 잡아둬서 초반 push 시 재할당 줄임.
    buffer_.reserve(kReadChunkSize * 4);
}

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

    std::cout << "[Camera] Started successfully\n";
    return true;
}

void Source::stop() {
    if (pipe_) {
        // pclose는 자식 프로세스의 종료를 기다린다 (SIGCHLD 처리도 같이).
        pclose(pipe_);
        pipe_ = nullptr;
        buffer_.clear();
        std::cout << "[Camera] Stopped\n";
    }
}

bool Source::find_start_code(std::size_t& pos, std::size_t& start_code_len) {
    // buffer_를 처음부터 훑으며 0x00 00 01 또는 0x00 00 00 01을 찾는다.
    //
    // 루프 한계 i+2<size 이유:
    //   3바이트 검사를 안전하게 하려면 인덱스 [i], [i+1], [i+2]가 모두 유효해야 함.
    //   4바이트(0x00 00 00 01) 검사는 안에서 별도로 인덱스 검사를 한 번 더 한다.
    for (std::size_t i = 0; i + 2 < buffer_.size(); ++i) {
        if (buffer_[i] == 0x00 && buffer_[i + 1] == 0x00) {
            // 3바이트 start code 우선 검사
            if (buffer_[i + 2] == 0x01) {
                pos = i;
                start_code_len = 3;
                return true;
            }
            // 4바이트 start code: 0x00 0x00 0x00 0x01
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

    // 청크 임시 버퍼. fread로 4KB씩 잘라 읽어 buffer_에 누적한다.
    uint8_t chunk[kReadChunkSize];

    while (true) {
        // ─── 1) 현재 buffer_에서 첫 번째 start code 찾기 ────────────────────
        std::size_t first_pos = 0, first_len = 0;
        if (!find_start_code(first_pos, first_len)) {
            // 아직 NAL 시작 표시조차 못 봤다 → 더 읽어서 buffer_를 키운다.
            std::size_t n = fread(chunk, 1, kReadChunkSize, pipe_);
            if (n == 0) {
                if (feof(pipe_)) {
                    return false;  // 카메라가 EOF — 스트림 종료
                }
                continue;  // 일시적으로 안 읽힌 것뿐, 다시 시도
            }
            buffer_.insert(buffer_.end(), chunk, chunk + n);
            continue;
        }

        // ─── 2) 두 번째 start code 찾기 (= NAL의 끝) ───────────────────────
        // NAL 데이터는 첫 start code 바로 뒤(nal_start)부터 시작해서
        // 다음 start code 직전까지.
        std::size_t nal_start     = first_pos + first_len;
        std::size_t search_offset = nal_start;

        while (true) {
            // search_offset부터 다음 start code 위치 검색.
            //
            // 주의: 이 안의 변수 `found`가 실제로 true로 갱신되는 코드가 빠져
            // 있다 (앞서 언급한 버그). 현재 동작은 NAL을 찾으면 즉시 return하기
            // 때문에 우연히 굴러가지만, 못 찾을 때 분기로 빠지는 로직이
            // 의도와 다르게 진행될 수 있다.
            bool found = false;
            for (std::size_t i = search_offset; i + 2 < buffer_.size(); ++i) {
                if (buffer_[i] == 0x00 && buffer_[i + 1] == 0x00) {
                    if (buffer_[i + 2] == 0x01 ||
                        (i + 3 < buffer_.size() && buffer_[i + 2] == 0x00 && buffer_[i + 3] == 0x01)) {
                        // 한 NAL 발견: nal_start ~ i-1 까지.
                        std::size_t nal_len = i - nal_start;
                        if (nal_len > 0) {
                            callback(buffer_.data() + nal_start, nal_len);
                        }
                        // 버퍼 앞을 잘라낸다. 잘라낸 후 buffer_[0]은 두 번째
                        // start code의 시작이므로 다음 read_nal 호출이 즉시
                        // 이걸 다시 첫 start code로 인식하게 된다.
                        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(i));
                        return true;
                    }
                }
            }

            if (!found) {
                // 다음 start code를 못 찾았으니 더 읽어서 buffer_ 확장.
                std::size_t n = fread(chunk, 1, kReadChunkSize, pipe_);
                if (n == 0) {
                    if (feof(pipe_)) {
                        // EOF에 도달했으면 남은 버퍼가 마지막 NAL.
                        if (buffer_.size() > nal_start) {
                            callback(buffer_.data() + nal_start, buffer_.size() - nal_start);
                            buffer_.clear();
                        }
                        return false;
                    }
                    continue;
                }
                buffer_.insert(buffer_.end(), chunk, chunk + n);

                // 다음 검색은 새로 추가된 부분을 포함해서 한다.
                // -3은 청크 경계에서 start code가 쪼개졌을 가능성 보완.
                // (단, 이 식은 search_offset이 buffer_.size()-n-3보다 작으면
                //  underflow가 날 수 있는 자리. nal_start 보호가 필요하다.)
                search_offset = buffer_.size() - n - 3;
                if (search_offset < nal_start) {
                    search_offset = nal_start;
                }
            }
        }
    }
}

}  // namespace veda::camera

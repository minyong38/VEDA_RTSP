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
// [start code 분할 처리]
//   fread는 4KB 청크 단위로 읽으므로 start code(3~4바이트)가 청크 경계에
//   걸쳐 쪼개질 수 있다. 그래서 새 데이터를 buffer_에 붙인 뒤에는 직전
//   버퍼 끝에서 3바이트 뒤로 물러나 재검색한다. 재검색 위치는 size_t
//   언더플로가 나지 않도록 항상 nal_start 이상으로 가드한다.

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

bool Source::find_start_code(std::size_t offset,
                             std::size_t& pos, std::size_t& start_code_len) const {
    // buffer_[offset..]를 훑으며 0x00 00 01 또는 0x00 00 00 01을 찾는다.
    //
    // 루프 한계 i+2<size 이유:
    //   3바이트 검사를 안전하게 하려면 인덱스 [i], [i+1], [i+2]가 모두 유효해야 함.
    //   4바이트(0x00 00 00 01) 검사는 안에서 별도로 인덱스 검사를 한 번 더 한다.
    for (std::size_t i = offset; i + 2 < buffer_.size(); ++i) {
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

    // 파이프에서 한 청크를 읽어 buffer_에 붙인다.
    //   true  = 데이터를 붙였음 (>0 바이트)
    //   false = EOF (스트림 종료)
    // n==0 이지만 EOF가 아닌 일시적 상황은 내부에서 재시도하여 블로킹한다.
    auto fill = [&]() -> bool {
        for (;;) {
            std::size_t n = fread(chunk, 1, kReadChunkSize, pipe_);
            if (n > 0) {
                buffer_.insert(buffer_.end(), chunk, chunk + n);
                return true;
            }
            if (feof(pipe_)) {
                return false;  // 카메라가 EOF — 스트림 종료
            }
            // n==0 && !feof : 일시적으로 안 읽힌 것뿐, 다시 시도
        }
    };

    // ─── 1) 첫 번째 start code를 버퍼 맨 앞에 정렬 ──────────────────────────
    // 정상 흐름에서는 직전 호출이 buffer_[0]에 start code를 남겨두므로 즉시
    // 찾는다. 첫 호출이거나 앞에 잡음이 낀 경우를 대비해 항상 한 번 정렬한다.
    std::size_t sc_pos = 0, sc_len = 0;
    while (!find_start_code(0, sc_pos, sc_len)) {
        if (!fill()) {
            return false;  // start code도 못 본 채 EOF
        }
    }
    // start code 앞의 쓰레기 바이트는 버린다. 이제 buffer_[0..sc_len)이 start code.
    if (sc_pos > 0) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(sc_pos));
    }

    // NAL 데이터는 첫 start code 바로 뒤부터 다음 start code 직전까지.
    const std::size_t nal_start = sc_len;

    // ─── 2) 두 번째 start code(= NAL의 끝) 찾기 ───────────────────────────
    std::size_t scan = nal_start;
    std::size_t next_pos = 0, next_len = 0;
    while (!find_start_code(scan, next_pos, next_len)) {
        // 못 찾았으니 더 읽는다. 단, start code가 청크 경계에 쪼개졌을 수
        // 있으므로, 새 데이터를 붙이기 전 버퍼 끝에서 최대 3바이트 뒤로
        // 물러난 지점부터 재검색한다. (언더플로 가드: 항상 nal_start 이상)
        std::size_t resume = buffer_.size() >= 3 ? buffer_.size() - 3 : nal_start;
        if (resume < nal_start) {
            resume = nal_start;
        }

        if (!fill()) {
            // EOF: 남은 버퍼 전체가 마지막 NAL.
            std::size_t tail = buffer_.size() - nal_start;
            if (tail > 0) {
                callback(buffer_.data() + nal_start, tail);
            }
            buffer_.clear();
            return false;
        }
        scan = resume;
    }

    // ─── 3) NAL 한 개 확정 → 콜백 → 버퍼 앞 잘라내기 ──────────────────────
    std::size_t nal_len = next_pos - nal_start;
    if (nal_len > 0) {
        callback(buffer_.data() + nal_start, nal_len);
    }
    // next_pos부터(= 다음 start code)를 버퍼 맨 앞으로 남긴다. 다음 호출이
    // 이걸 즉시 첫 start code로 인식하므로 1)의 fill 없이 빠르게 진행된다.
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(next_pos));
    return true;
}

}  // namespace veda::camera

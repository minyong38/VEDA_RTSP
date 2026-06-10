// src/media/annexb_reader.cpp
//
// Annex-B NAL 분리 로직. 원래 camera/source.cpp에 있던 구현을 그대로
// 옮겨와 FILE* 출처에 무관하게 재사용할 수 있도록 일반화했다.
//
// [데이터 흐름]
//
//   FILE* (popen 또는 fopen)
//        │
//        ▼  (fread, 4KB 청크씩)
//   buffer_  ─── start code 찾기 ─── 두 start code 사이가 NAL 한 개 ───→ callback
//        │
//        └─ erase로 앞부분 잘라 다음 NAL 준비

#include "media/annexb_reader.hpp"

namespace veda::media {

AnnexBReader::AnnexBReader() {
    // 미리 4 청크 정도 잡아둬서 초반 push 시 재할당 줄임.
    buffer_.reserve(kReadChunkSize * 4);
}

void AnnexBReader::reset() {
    buffer_.clear();
}

bool AnnexBReader::find_start_code(std::size_t offset,
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

bool AnnexBReader::read_nal(FILE* fp, const NalCallback& callback) {
    if (!fp) {
        return false;
    }

    // 청크 임시 버퍼. fread로 4KB씩 잘라 읽어 buffer_에 누적한다.
    uint8_t chunk[kReadChunkSize];

    // 파이프/파일에서 한 청크를 읽어 buffer_에 붙인다.
    //   true  = 데이터를 붙였음 (>0 바이트)
    //   false = EOF (스트림 종료)
    // n==0 이지만 EOF가 아닌 일시적 상황은 내부에서 재시도하여 블로킹한다.
    auto fill = [&]() -> bool {
        for (;;) {
            std::size_t n = fread(chunk, 1, kReadChunkSize, fp);
            if (n > 0) {
                buffer_.insert(buffer_.end(), chunk, chunk + n);
                return true;
            }
            if (feof(fp) || ferror(fp)) {
                return false;  // EOF 또는 에러 — 스트림 종료
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

}  // namespace veda::media

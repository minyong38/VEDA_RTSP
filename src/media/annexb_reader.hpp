// src/media/annexb_reader.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// FILE* 스트림에서 H.264 Annex-B NAL을 한 개씩 잘라내는 헬퍼.
// ─────────────────────────────────────────────────────────────────────────────
//
// [왜 분리했나]
//   "start code를 찾아 NAL 경계를 긋는" 로직은 출처가 카메라 파이프(popen)든
//   파일(fopen)이든 완전히 동일하다. camera::Source 안에 박혀 있던 것을
//   여기로 빼서 FileSource와 공유한다.
//
// [start code 두 가지]
//   - 3-byte: 0x00 0x00 0x01           (대부분의 NAL)
//   - 4-byte: 0x00 0x00 0x00 0x01      (스트림 시작/SPS·PPS 앞 등)
//   Annex-B 포맷에서는 NAL과 NAL 사이에 둘 중 하나가 들어간다.
//
// [start code 분할 처리]
//   fread는 청크 단위로 읽으므로 start code(3~4바이트)가 청크 경계에
//   걸쳐 쪼개질 수 있다. 새 데이터를 붙인 뒤에는 직전 버퍼 끝에서 3바이트
//   뒤로 물러나 재검색한다 (언더플로 가드 포함).
//
// [소유권]
//   FILE*는 빌리기만 한다. popen/fopen과 pclose/fclose는 호출 측 책임.

#pragma once

#include "media/nal_source.hpp"

#include <cstdio>
#include <vector>

namespace veda::media {

class AnnexBReader {
public:
    AnnexBReader();

    // fp에서 다음 NAL 한 개를 읽어 콜백 호출.
    // 반환값: true=정상, false=EOF (마지막 잔여 NAL이 있으면 콜백 후 false)
    bool read_nal(FILE* fp, const NalCallback& callback);

    // 내부 누적 버퍼를 비운다. 파일을 rewind해서 재사용할 때 필수 —
    // 안 비우면 이전 파일 끝자락과 새 데이터가 이어붙어 깨진 NAL이 나온다.
    void reset();

private:
    // buffer_[offset..] 구간에서 start code를 찾아 위치/길이를 알려준다.
    bool find_start_code(std::size_t offset,
                         std::size_t& pos, std::size_t& start_code_len) const;

    // NAL 파싱용 누적 버퍼. fread가 청크 단위로 채우고, find_start_code가
    // 그 안에서 NAL 경계를 그어 콜백으로 토해낸 뒤 erase로 앞을 잘라낸다.
    std::vector<uint8_t> buffer_;
    static constexpr std::size_t kReadChunkSize = 4096;
};

}  // namespace veda::media

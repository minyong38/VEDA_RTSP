// src/rtp/h264_fua.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// H.264 NAL을 RTP 페이로드 크기로 자르는 분할기 (RFC 6184).
// ─────────────────────────────────────────────────────────────────────────────
//
// [왜 분할이 필요한가]
//   이더넷 MTU ≈ 1500 byte. IP(20) + UDP(8) + RTP(12) = 40 byte 헤더를 빼면
//   페이로드로 쓸 수 있는 건 약 1460. 안전 마진을 두고 우리는 1400을 쓴다.
//   그런데 H.264 IDR(키프레임) 한 장은 수십 KB가 보통이다 → 그대로 못 보냄.
//   조각내야 하는데, 그 조각을 받는 쪽에서 어떻게 재조립할지 약속이 필요.
//
// [RFC 6184가 정의하는 H.264 over RTP 모드 3가지]
//   1) Single NAL Unit       : NAL이 MTU보다 작을 때, 그냥 통째로
//   2) Aggregation (STAP-A 등): 작은 NAL 여러 개를 한 RTP에 합치기 (우리는 안 씀)
//   3) Fragmentation (FU-A)   : 큰 NAL을 여러 RTP로 쪼개기 ← 우리가 쓰는 모드
//
// [FU-A 패킷 구조]
//
//   원래 NAL:
//     [NAL 헤더 1B = F(1) | NRI(2) | Type(5)]  [페이로드 N B]
//                            ↓
//   조각화 후 각 RTP 페이로드:
//     [FU indicator 1B] [FU header 1B] [페이로드 일부]
//
//   FU indicator = (원본 NAL의 F|NRI 3비트) | 28
//                  └ 28 = "이건 FU-A 타입이다" 라는 표시
//
//   FU header    = S(1) | E(1) | R(1) | (원본 NAL의 Type 5비트)
//                  └ S: Start bit (첫 조각이면 1)
//                  └ E: End bit   (마지막 조각이면 1)
//                  └ R: Reserved (항상 0)
//
//   중요: 원본 NAL 헤더의 정보가 두 바이트(indicator + header)로 흩어진다.
//         받는 쪽은 (FU indicator의 F|NRI) | (FU header의 하위 5비트)로
//         원본 NAL 헤더를 복원해 디코더에 먹인다.
//
// [입력 NAL의 첫 바이트]
//   start code(0x00 00 00 01)는 이미 호출 측에서 떼고 들어왔다고 가정.
//   nal[0]이 진짜 NAL 헤더 바이트.

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace veda::rtp {

// 일반적인 이더넷에서 안전한 RTP 페이로드 상한.
// 이 값보다 큰 NAL은 자동으로 FU-A로 잘린다.
constexpr std::size_t kDefaultMtu = 1400;

// 입력: NAL 한 개의 바이트들 (헤더 포함, start code 제외)
// 출력: RTP 페이로드로 곧장 쓸 수 있는 바이트 묶음들의 리스트
//
//   - NAL ≤ mtu → 1개 묶음 (NAL 그대로) = Single NAL Unit 모드
//   - NAL > mtu → N개 묶음 (각각 FU indicator + FU header + payload chunk)
//
// 반환된 각 묶음은 그대로 RTP 헤더 뒤에 붙여서 UDP로 쏘면 된다.
// 같은 NAL에서 나온 묶음들은 같은 RTP timestamp를 써야 하고, 마지막 묶음에만
// RTP marker bit를 1로 세팅해야 한다 (호출 측 streamer.cpp 책임).
std::vector<std::vector<uint8_t>> fragment_nal(const uint8_t* nal,
                                               std::size_t nal_len,
                                               std::size_t mtu = kDefaultMtu);

}  // namespace veda::rtp

# VEDA_RTSP

> 외부 미디어 라이브러리 없이 처음부터 구현한 RTSP/RTP 영상 스트리밍 서버.
> POSIX 소켓과 RFC 문서만으로 H.264 비디오를 표준 클라이언트(ffplay, VLC)에 전송합니다.

[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/CMake-3.16%2B-brightgreen.svg)](https://cmake.org/)
[![Status](https://img.shields.io/badge/status-WIP-orange.svg)](#progress)

---

## 왜 이걸 만드는가

RTSP/RTP는 IP 카메라·NVR·드론 영상 전송의 사실상 표준이지만,
대부분의 프로젝트는 **Live555**나 **GStreamer**로 추상화한 채 쓴다.
이 프로젝트는 그 추상화를 걷어내고 다음을 직접 다룬다:

- RTP 12바이트 헤더의 비트 패킹과 시퀀스/타임스탬프 관리 (RFC 3550)
- H.264 NAL 유닛을 MTU에 맞춰 **FU-A 분할**하는 페이로드 포맷 (RFC 6184)
- RTSP 메서드별 세션 상태머신: `OPTIONS → DESCRIBE → SETUP → PLAY` (RFC 2326)
- TCP 제어 채널과 UDP 데이터 채널의 분리 운영

목표는 "동작하는 서버" 자체가 아니라 **`tcpdump -X`로 패킷을 찍었을 때 한 바이트씩 설명할 수 있는 수준**의 이해다.

## 주요 기능

- [ ] RTSP 1.0 요청 파서 (OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN)
- [ ] DESCRIBE 응답용 SDP 본문 생성
- [ ] RTP 패킷화 + H.264 FU-A 분할
- [ ] UDP를 통한 RTP 영상 송출
- [ ] 다중 클라이언트 동시 접속 (Week 5 이후)
- [ ] RTCP Sender Report로 동기화 정보 전송 (Week 7 이후)

## 데모

> 첫 프레임 송출 성공 시 GIF 첨부 예정 (Week 4 종료 시점)
>
> ```bash
> $ ffplay -rtsp_transport udp rtsp://localhost:8554/stream
> ```

## 빌드 & 실행

### 요구사항

- C++17 컴파일러 (GCC 9+ / Clang 10+)
- CMake 3.16+
- POSIX 환경 (Linux 권장, macOS 동작)
- 테스트용: ffmpeg (ffplay 포함)

### 빌드

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### 실행

```bash
./build/veda_rtsp --port 8554 --source tools/samples/test.h264
```

### 클라이언트 연결

```bash
ffplay -rtsp_transport udp rtsp://localhost:8554/stream
# 또는
vlc rtsp://localhost:8554/stream
```

### 테스트

```bash
cd build && ctest --output-on-failure
```

## 프로젝트 구조

```
VEDA_RTSP/
├── CMakeLists.txt          # 최상위 빌드 설정
├── README.md
├── .gitignore
│
├── src/
│   ├── main.cpp            # 진입점
│   ├── rtsp/
│   │   ├── server.{hpp,cpp}    # TCP accept 루프, 세션 생성
│   │   ├── parser.{hpp,cpp}    # RTSP 요청 라인 + 헤더 파싱
│   │   └── session.{hpp,cpp}   # 세션 상태머신
│   ├── sdp/
│   │   └── builder.{hpp,cpp}   # DESCRIBE 응답용 SDP 본문
│   ├── rtp/
│   │   ├── packetizer.{hpp,cpp}  # RTP 12바이트 헤더 작성
│   │   └── h264_fua.{hpp,cpp}    # H.264 NAL → FU-A 분할
│   └── net/
│       ├── tcp_socket.{hpp,cpp}  # POSIX TCP 래퍼
│       └── udp_socket.{hpp,cpp}  # POSIX UDP 래퍼
│
├── tests/                  # 모듈별 단위 테스트
├── tools/                  # ffplay 스모크 테스트 등
└── docs/
    ├── architecture.md     # 컴포넌트 다이어그램
    ├── state-machine.md    # 세션 상태 전이표
    └── design-decisions.md # 설계 결정 + 면접 대비 메모
```

자세한 구조 설명은 [docs/architecture.md](docs/architecture.md) 참고.

## 진행 상황

8주 일정으로 진행 중. 자세한 일정은 기획안 참고.

| 주차 | 목표 | 상태 |
|------|------|------|
| Week 1 | TCP 서버 + RTSP 요청 파서 | 🟡 진행 중 |
| Week 2 | DESCRIBE → SDP 응답 | ⬜ |
| Week 3 | ⭐ SETUP + 첫 RTP 패킷 송출 | ⬜ |
| Week 4 | H.264 FU-A 분할 + 영상 재생 | ⬜ |
| Week 5 | 다중 클라이언트 동시 접속 | ⬜ |
| Week 6 | 성능 측정 + 최적화 | ⬜ |
| Week 7 | RTCP Sender Report | ⬜ |
| Week 8 | 문서 정리 + 포트폴리오화 | ⬜ |

## 참고 표준

- [RFC 2326](https://datatracker.ietf.org/doc/html/rfc2326) — RTSP 1.0
- [RFC 3550](https://datatracker.ietf.org/doc/html/rfc3550) — RTP
- [RFC 4566](https://datatracker.ietf.org/doc/html/rfc4566) — SDP
- [RFC 6184](https://datatracker.ietf.org/doc/html/rfc6184) — H.264 RTP payload format

## 라이선스

학습 목적 프로젝트. MIT.

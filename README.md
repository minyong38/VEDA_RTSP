# VEDA_RTSP

> RTSP 시그널링과 RTP/H.264 전송을 외부 미디어 라이브러리(Live555·GStreamer·FFmpeg) 없이
> RFC 문서만 보고 POSIX 소켓으로 처음부터 구현하는 프로젝트.

[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/CMake-3.16%2B-brightgreen.svg)](https://cmake.org/)
[![Status](https://img.shields.io/badge/status-WIP-orange.svg)](#progress)

---

## 왜 이걸 만드는가

RTSP는 IP 카메라·NVR·드론 영상 전송의 사실상 표준 시그널링 프로토콜이다.
대부분의 프로젝트는 **Live555**나 **GStreamer**로 추상화한 채 쓰지만,
이 프로젝트는 **RTSP 시그널링부터 RTP 패킷화까지 직접 구현**하여
프로토콜을 한 바이트 단위로 이해하는 것이 목표다:

- RTSP 메서드별 세션 상태머신: `OPTIONS → DESCRIBE → SETUP → PLAY` (RFC 2326)
- SDP(Session Description Protocol) 동적 생성 (RFC 4566)
- TCP 제어 채널 운영 및 클라이언트 세션 관리
- RTP 12바이트 헤더 작성 + H.264 NAL의 FU-A 분할 직접 구현 (RFC 3550 / 6184)

목표는 "RTSP 핸드셰이크와 RTP 패킷을 `tcpdump`로 찍었을 때 모든 헤더를 한 바이트씩 설명할 수 있는 수준"의 이해다.

## 주요 기능

- [x] 프로젝트 구조 설계
- [x] RTSP 1.0 요청 파서 (OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN)
- [x] RTSP 응답 생성기
- [x] DESCRIBE 응답용 SDP 본문 생성
- [x] RTP 패킷화 직접 구현 (RFC 3550 헤더 + RFC 6184 FU-A 분할)
- [x] 카메라 소스 연동 (libcamera-vid → H.264 NAL → RTP 송출)
- [ ] 세션 상태머신 강제 (현재는 미강제 — Session 클래스 활성화 예정)
- [ ] 멀티클라이언트 동시 접속 (epoll 기반)

## 데모

> 첫 RTSP 핸드셰이크 성공 시 캡처 첨부 예정
>
> ```bash
> $ ffplay -rtsp_transport udp rtsp://localhost:8554/stream
> ```

## 빌드 & 실행

### 요구사항

- C++17 컴파일러 (GCC 9+ / Clang 10+)
- CMake 3.16+
- POSIX 환경 (Linux 권장, macOS 동작)
- 테스트용: ffmpeg (ffplay 포함), VLC

### 빌드

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### 실행

```bash
./build/veda_rtsp --port 8554
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
│   │   ├── server.{hpp,cpp}     # TCP accept 루프 + 메서드 디스패치
│   │   ├── parser.{hpp,cpp}     # RTSP 요청 라인 + 헤더 파싱
│   │   ├── response.{hpp,cpp}   # RTSP 응답 생성
│   │   └── session.{hpp,cpp}    # 세션 상태머신 (예정)
│   ├── sdp/
│   │   └── builder.{hpp,cpp}    # DESCRIBE 응답용 SDP 본문
│   ├── rtp/
│   │   ├── packetizer.{hpp,cpp} # RTP 12바이트 헤더 (RFC 3550)
│   │   ├── h264_fua.{hpp,cpp}   # H.264 NAL → FU-A 분할 (RFC 6184)
│   │   └── streamer.{hpp,cpp}   # NAL → RTP → UDP 송출 접착제
│   ├── camera/
│   │   └── source.{hpp,cpp}     # libcamera-vid → H.264 NAL 추출
│   └── net/
│       ├── tcp_socket.{hpp,cpp} # POSIX TCP 래퍼
│       └── udp_socket.{hpp,cpp} # POSIX UDP 래퍼 (RTP 전송)
│
├── tests/                  # 모듈별 단위 테스트
├── tools/                  # 테스트 스크립트 + 브라우저 시뮬레이터
└── docs/
    ├── architecture.md     # 컴포넌트 다이어그램
    ├── state-machine.md    # 세션 상태 전이표
    └── design-decisions.md # 설계 결정 + 면접 대비 메모
```

자세한 구조 설명은 [docs/architecture.md](docs/architecture.md) 참고.

## 진행 상황

| 주차 | 목표 | 상태 |
|------|------|------|
| Week 1 | TCP 서버 + RTSP 요청 파서 | ✅ 완료 |
| Week 2 | DESCRIBE → SDP 응답, SETUP 핸들러 | ✅ 완료 |
| Week 3 | PLAY/TEARDOWN + RTP 송출 | ✅ 완료 |
| Week 4 | H.264 FU-A 분할 + 카메라 연동 (첫 영상 재생) | ✅ 완료 |
| Week 5 | 세션 상태머신 강제 + 멀티클라이언트 (epoll) | 🟡 진행 중 |
| Week 6 | 안정화 + 에러 처리 (카메라 NAL 파서 등) | ⬜ |
| Week 7 | 성능 측정 + 최적화 | ⬜ |
| Week 8 | 문서 정리 + 포트폴리오화 | ⬜ |

## 참고 표준

- [RFC 2326](https://datatracker.ietf.org/doc/html/rfc2326) — RTSP 1.0
- [RFC 4566](https://datatracker.ietf.org/doc/html/rfc4566) — SDP
- [RFC 3550](https://datatracker.ietf.org/doc/html/rfc3550) — RTP 헤더 (직접 구현)
- [RFC 6184](https://datatracker.ietf.org/doc/html/rfc6184) — H.264 RTP payload format / FU-A (직접 구현)

## 라이선스

학습 목적 프로젝트. MIT.

# VEDA_RTSP

> RTSP 프로토콜을 RFC 2326 표준 문서를 보고 처음부터 구현하는 프로젝트.
> RTP 영상 전송은 검증된 라이브러리를 활용하여 RTSP 시그널링 구현에 집중합니다.

[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/CMake-3.16%2B-brightgreen.svg)](https://cmake.org/)
[![Status](https://img.shields.io/badge/status-WIP-orange.svg)](#progress)

---

## 왜 이걸 만드는가

RTSP는 IP 카메라·NVR·드론 영상 전송의 사실상 표준 시그널링 프로토콜이다.
대부분의 프로젝트는 **Live555**나 **GStreamer**로 추상화한 채 쓰지만,
이 프로젝트는 **RTSP 시그널링 계층을 직접 구현**하여 프로토콜을 깊이 이해하는 것이 목표다:

- RTSP 메서드별 세션 상태머신: `OPTIONS → DESCRIBE → SETUP → PLAY` (RFC 2326)
- SDP(Session Description Protocol) 동적 생성 (RFC 4566)
- TCP 제어 채널 운영 및 클라이언트 세션 관리
- RTP 전송은 라이브러리(live555/GStreamer)를 활용하여 안정성 확보

목표는 "RTSP 시그널링을 `tcpdump`로 찍었을 때 모든 헤더를 설명할 수 있는 수준"의 이해다.

## 주요 기능

- [x] 프로젝트 구조 설계
- [ ] RTSP 1.0 요청 파서 (OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN)
- [ ] RTSP 응답 생성기
- [ ] DESCRIBE 응답용 SDP 본문 생성
- [ ] 세션 상태머신 구현
- [ ] 멀티클라이언트 동시 접속 (epoll 기반)
- [ ] RTP 라이브러리 연동 (live555 또는 GStreamer)

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
│   │   ├── server.{hpp,cpp}    # TCP accept 루프, 세션 생성
│   │   ├── parser.{hpp,cpp}    # RTSP 요청 라인 + 헤더 파싱
│   │   ├── response.{hpp,cpp}  # RTSP 응답 생성
│   │   └── session.{hpp,cpp}   # 세션 상태머신
│   ├── sdp/
│   │   └── builder.{hpp,cpp}   # DESCRIBE 응답용 SDP 본문
│   └── net/
│       └── tcp_socket.{hpp,cpp}  # POSIX TCP 래퍼
│
├── tests/                  # 모듈별 단위 테스트
├── tools/                  # 테스트 스크립트
└── docs/
    ├── architecture.md     # 컴포넌트 다이어그램
    ├── state-machine.md    # 세션 상태 전이표
    └── design-decisions.md # 설계 결정 + 면접 대비 메모
```

자세한 구조 설명은 [docs/architecture.md](docs/architecture.md) 참고.

## 진행 상황

| 주차 | 목표 | 상태 |
|------|------|------|
| Week 1 | TCP 서버 + RTSP 요청 파서 | 🟡 진행 중 |
| Week 2 | DESCRIBE → SDP 응답, SETUP 핸들러 | ⬜ |
| Week 3 | PLAY/TEARDOWN + 세션 상태머신 완성 | ⬜ |
| Week 4 | RTP 라이브러리 연동 + 첫 영상 재생 | ⬜ |
| Week 5 | 멀티클라이언트 동시 접속 (epoll) | ⬜ |
| Week 6 | 안정화 + 에러 처리 | ⬜ |
| Week 7 | 성능 측정 + 최적화 | ⬜ |
| Week 8 | 문서 정리 + 포트폴리오화 | ⬜ |

## 참고 표준

- [RFC 2326](https://datatracker.ietf.org/doc/html/rfc2326) — RTSP 1.0
- [RFC 4566](https://datatracker.ietf.org/doc/html/rfc4566) — SDP
- [RFC 3550](https://datatracker.ietf.org/doc/html/rfc3550) — RTP (라이브러리가 처리)
- [RFC 6184](https://datatracker.ietf.org/doc/html/rfc6184) — H.264 RTP payload format (라이브러리가 처리)

## 라이선스

학습 목적 프로젝트. MIT.

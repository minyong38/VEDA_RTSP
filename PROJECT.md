# VEDA_RTSP — Project Context

## Project Overview

VEDA_RTSP는 외부 미디어 라이브러리(Live555, GStreamer 등) 없이 POSIX 소켓과 RFC 문서만으로 처음부터 구현하는 RTSP/RTP H.264 영상 스트리밍 서버다. 한화비전 Network Camera 펌웨어 직무 포트폴리오용 프로젝트(EdgeTrack PTZ)의 핵심 파트.

### 목표

- `tcpdump -X`로 패킷을 찍었을 때 한 바이트씩 설명할 수 있는 수준의 프로토콜 이해
- VLC, ffplay 등 표준 클라이언트에서 H.264 영상 재생
- 한화비전 SUNAPI 호환 HTTP 인터페이스 구현
- ONVIF Profile S 기본 호환

## Tech Stack

- **Language**: C++17
- **Build**: CMake 3.16+
- **Network**: POSIX 소켓 직접 사용 (socket, bind, listen, accept, epoll)
- **Target**: Linux (라즈베리파이), POSIX 환경
- **Test**: GoogleTest (tests/ 디렉토리)
- **의도적 배제**: Live555, GStreamer, FFmpeg, Boost.Asio — 학습 목적으로 직접 구현

## Architecture

```
src/
├── main.cpp              # 진입점 (--port, --source camera|file.h264)
├── rtsp/
│   ├── server.{hpp,cpp}  # epoll 이벤트 루프, accept, fd→Session 라우팅
│   ├── parser.{hpp,cpp}  # RTSP 요청 라인 + 헤더 파싱
│   ├── response.{hpp,cpp}# RTSP 응답 생성
│   └── session.{hpp,cpp} # 클라이언트별 상태머신 (INIT→READY→PLAYING→CLOSED, 455/454 강제)
├── sdp/
│   └── builder.{hpp,cpp} # DESCRIBE 응답용 SDP 본문 생성 (SPS/PPS Base64 포함)
├── rtp/
│   ├── packetizer.{hpp,cpp}  # RTP 12바이트 헤더 작성 (RFC 3550)
│   ├── h264_fua.{hpp,cpp}    # H.264 NAL → FU-A 분할 (RFC 6184)
│   └── streamer.{hpp,cpp}    # NAL → RTP → UDP 송출 (세션별 SSRC/seq/ts)
├── media/
│   ├── nal_source.hpp        # 소스 추상 인터페이스 (start/read_nal/stop)
│   ├── annexb_reader.{hpp,cpp} # Annex-B NAL 경계 분리 (카메라/파일 공용)
│   ├── file_source.{hpp,cpp} # .h264 파일 + fps 페이싱 + 루프 재생
│   └── hub.{hpp,cpp}         # 소스 1개 → 구독자 N명 fan-out (펌프 스레드)
├── camera/
│   └── source.{hpp,cpp}      # libcamera-vid popen → H.264 NAL 추출
├── util/
│   └── base64.{hpp,cpp}      # sprop-parameter-sets 인코딩
└── net/
    ├── tcp_socket.{hpp,cpp}  # POSIX TCP 래퍼
    └── udp_socket.{hpp,cpp}  # POSIX UDP 래퍼 (RTP 전송)

tests/
├── test_parser.cpp   # RTSP 파서 단위 테스트
├── test_rtp.cpp      # RTP 패킷화 테스트
├── test_sdp.cpp      # SDP 빌더 테스트
└── test_session.cpp  # 세션 상태머신 테스트 (455/454/461, 단편화/파이프라이닝)
```

## Key Protocols & RFCs

| RFC | 용도 |
|-----|------|
| RFC 2326 | RTSP 1.0 시그널링 (TCP 텍스트 프로토콜) |
| RFC 3550 | RTP 헤더 구조 (12바이트, 90kHz clock) |
| RFC 4566 | SDP 문법 |
| RFC 6184 | H.264 RTP payload format (FU-A 분할) |

## RTSP Session State Machine

```
INIT → (OPTIONS) → INIT
INIT → (SETUP)   → READY
READY → (PLAY)   → PLAYING
PLAYING → (PAUSE) → READY
READY/PLAYING → (TEARDOWN) → CLOSED
```

- 잘못된 상태 메서드 → `455 Method Not Valid in This State`
- 알 수 없는 메서드 → `501 Not Implemented`
- 파싱 실패 → `400 Bad Request`

## RTP Packetization Rules

- MTU 기준 MAX_PAYLOAD = 1400 - 12 = 1388 바이트
- NAL ≤ MAX_PAYLOAD → Single NAL Unit Mode
- NAL > MAX_PAYLOAD → FU-A Fragmentation
  - FU indicator: NRI | 28 (type=28)
  - FU header: Start bit(7) | End bit(6) | NAL type(0-4)
- Timestamp: 90kHz, 같은 프레임은 동일 timestamp (30fps → 프레임당 3000)
- Marker bit: 프레임의 마지막 패킷에만 1
- SSRC: 세션 시작 시 랜덤 32비트

## SDP Generation Notes

- `sprop-parameter-sets`: SPS/PPS를 Base64 인코딩해서 삽입
- `profile-level-id`: 인코더 실제 프로파일과 일치 필수
- `packetization-mode=1`: FU-A 사용 가능 모드
- `a=control:track1` (또는 `trackID=1`)

## Build & Run

```bash
# 빌드
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 실행 (라즈베리파이 카메라)
./build/veda_rtsp --port 8554

# 실행 (카메라 없는 PC — H.264 파일 루프 재생)
./build/veda_rtsp --port 8554 --source test.h264

# 클라이언트 접속 (여러 개 동시 접속 가능)
ffplay -rtsp_transport udp rtsp://localhost:8554/stream

# 테스트
cd build && ctest --output-on-failure
```

## SUNAPI Compatibility (향후 구현)

한화비전 SUNAPI(Smart Unified Network API) 호환 HTTP 서버. URL 포맷:
```
http://<ip>/stw-cgi/<category>.cgi?msubmenu=<feature>&action=<action>[&param=value]
```

구현 대상 엔드포인트:
- `system.cgi?msubmenu=deviceinfo&action=view` — 기기 정보
- `system.cgi?msubmenu=network&action=view` — 네트워크 정보
- `ptzcontrol.cgi?msubmenu=continuous&action=control` — PTZ 연속 이동
- `ptzcontrol.cgi?msubmenu=stop&action=control` — PTZ 정지
- `image.cgi?msubmenu=focus&action=view` — 포커스 정보
- `eventsources.cgi?msubmenu=objectdetection` — 객체 감지

응답 형식: 텍스트(key=value) / JSON 두 가지 지원.

## RTSP URL Patterns (듀얼 라우팅)

```
# 표준/심플
rtsp://<ip>:8554/live
rtsp://<ip>:8554/stream

# 한화 호환
rtsp://<ip>:8554/profile<no>/media.smp

# ONVIF 호환
rtsp://<ip>:554/<chid>/onvif/profile<no>/media.smp
```

## Coding Conventions

- C++17 기능 적극 활용: structured bindings, std::optional, std::string_view
- 외부 의존성 최소화 원칙 (RTSP/RTP는 표준 라이브러리만으로 구현)
- 네트워크 바이트 오더(big-endian) 주의: htons, htonl 사용
- 헤더 비트 패킹은 수동 시프트/마스크 (bitfield 대신)
- 에러 처리: POSIX 소켓 에러는 errno 확인 후 적절한 RTSP 상태 코드 반환

## Git Commit Rules

- 커밋 메시지에 AI 보조 도구 trailer (Co-Authored-By, Generated with 등) 를 추가하지 마세요.
- 사람이 작성한 것처럼 본문만 깔끔하게 남겨주세요.

## Progress (8주 일정)

| Week | 목표 | 상태 |
|------|------|------|
| 1 | TCP 서버 + RTSP 요청 파서 | ✅ 완료 |
| 2 | DESCRIBE → SDP 응답, SETUP 핸들러 | ✅ 완료 |
| 3 | RTP 패킷 송출 + PLAY (VLC 첫 재생) | ✅ 완료 |
| 4 | H.264 FU-A 분할 + 영상 재생 | ✅ 완료 |
| 5 | 세션 상태머신 강제 + 멀티클라이언트 (epoll + Hub) | ✅ 완료 |
| 6 | 안정화 (파일 소스 모드, PAUSE/GET_PARAMETER, 끊김 정리) | ✅ 완료 |
| 7 | SUNAPI HTTP 서버 / 성능 측정 | - |
| 8 | 데모 영상 + 포트폴리오 정리 | - |

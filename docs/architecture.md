# Architecture

> RTSP 시그널링과 RTP/H.264 전송을 외부 미디어 라이브러리 없이 직접 구현한 구조.

## Overview

VEDA_RTSP는 RTSP 시그널링(RFC 2326)과 RTP 패킷화(RFC 3550), H.264 FU-A 분할(RFC 6184)을
모두 POSIX 소켓과 표준 라이브러리만으로 직접 구현한 스트리밍 서버다.
epoll 기반 이벤트 루프로 여러 클라이언트를 동시에 처리하며, 카메라 1대(또는 H.264 파일 1개)의
영상을 media::Hub가 모든 시청자에게 분배한다.

## Component Diagram

```
┌────────────────────────────────────────────────────────────────────┐
│                            veda_rtsp                               │
│                                                                    │
│  Client A ──┐                                                      │
│  Client B ──┤ TCP 8554   rtsp::Server (epoll 이벤트 루프)           │
│  Client C ──┘                │                                     │
│  (VLC/ffplay)                │ fd별로                              │
│                              ▼                                     │
│                        rtsp::Session ×N  (클라이언트당 1개)         │
│                          │  상태머신: INIT→READY→PLAYING→CLOSED    │
│                          │  (parser / response / sdp::builder 사용) │
│                          │                                         │
│                          │ PLAY: 자기 Streamer를 Hub에 구독          │
│                          ▼                                         │
│   camera::Source ──┐                                               │
│   (libcamera-vid)  ├──▶ media::Hub ──▶ rtp::Streamer ×N            │
│   media::FileSource┘    (펌프 스레드,    │  (세션별 SSRC/seq/ts)     │
│   (.h264 + 페이싱)       NAL fan-out)    │                          │
│                                          ▼                         │
│                              rtp::Packetizer + h264_fua            │
│                                          │                         │
│                                          ▼                         │
│                              net::UdpSocket ── UDP ──▶ Client들     │
└────────────────────────────────────────────────────────────────────┘
```

## 책임 분리

### RTSP 시그널링 (직접 구현)

| Module            | 책임 |
|-------------------|------|
| `rtsp/server`     | epoll 이벤트 루프, accept, fd→Session 라우팅 |
| `rtsp/parser`     | RTSP 요청 라인 + 헤더 파싱 |
| `rtsp/response`   | RTSP 응답 메시지 생성 |
| `rtsp/session`    | 클라이언트별 상태머신 강제 (455/454), 메서드 핸들러 |
| `sdp/builder`     | DESCRIBE 응답용 SDP 본문 생성 (sprop 포함) |
| `net/tcp_socket`  | POSIX TCP 래퍼 (RAII) |

### RTP 전송 (직접 구현)

| Module            | 책임 |
|-------------------|------|
| `rtp/packetizer`  | RTP 12바이트 헤더 작성 (RFC 3550) |
| `rtp/h264_fua`    | H.264 NAL → FU-A 분할 (RFC 6184) |
| `rtp/streamer`    | NAL → RTP 패킷 → UDP 송출 접착제 (세션별 상태) |
| `net/udp_socket`  | POSIX UDP 래퍼 |

### 미디어 소스 (직접 구현)

| Module                | 책임 |
|-----------------------|------|
| `media/nal_source`    | 소스 추상 인터페이스 (start/read_nal/stop) |
| `media/annexb_reader` | Annex-B start code 기준 NAL 경계 분리 (공용) |
| `camera/source`       | libcamera-vid popen → NAL 추출 |
| `media/file_source`   | .h264 파일 + fps 페이싱 + 루프 재생 (개발 PC용) |
| `media/hub`           | 소스 1개 → 구독자 N명 fan-out, SPS/PPS 캐시 |
| `util/base64`         | sprop-parameter-sets 인코딩 |

## 멀티클라이언트 데이터 흐름

```
1. 클라이언트 → TCP 연결 → epoll이 감지 → Server가 accept + Session 생성
2. OPTIONS/DESCRIBE/SETUP: epoll 스레드에서 Session이 즉시 응답
   (상태머신 위반 시 455, 세션 ID 불일치 시 454)
3. PLAY: Session이 rtp::Streamer를 만들어 클라이언트 주소에 connect 후
   media::Hub에 구독 → 즉시 반환 (epoll 루프는 계속 돈다)
4. Hub 펌프 스레드: 소스에서 NAL을 읽는 족족 구독자 전원의 Streamer에 분배
   각 Streamer가 독립적인 SSRC/sequence/timestamp로 RTP 패킷 생성 → UDP 송출
5. PAUSE: 구독 해제 (READY 복귀) / TEARDOWN·연결 끊김: 구독 해제 + 자원 정리
6. 마지막 시청자가 나가면 Hub가 소스(카메라 프로세스)를 끈다
```

## 스레드 모델

| 스레드 | 하는 일 | 공유 자원 접근 |
|--------|---------|----------------|
| epoll 스레드 (메인) | accept, RTSP 요청 처리, Hub 구독/해제 | Hub의 mutex로 보호 |
| Hub 펌프 스레드 | 소스 read_nal → 구독자 fan-out | 〃 |

구독자 목록과 SPS/PPS 캐시는 mutex 하나로 보호한다. `unsubscribe()`가 반환되면
펌프 스레드가 해당 Streamer를 더 이상 만지지 않음이 보장되므로 use-after-free가 없다.

## 외부 의존성

**없음.** RTSP/RTP/SDP/Base64 모두 C++17 표준 라이브러리 + POSIX API만 사용.
(카메라 캡처만 libcamera-vid 외부 프로세스를 popen으로 빌려 쓴다 — 프로토콜 구현이
본 주제이고 카메라 드라이버는 범위 밖이라는 판단.)

## 참고 RFC

- RFC 2326 — RTSP 1.0 (직접 구현)
- RFC 4566 — SDP (직접 구현)
- RFC 3550 — RTP 헤더 (직접 구현)
- RFC 6184 — H.264 RTP payload format / FU-A (직접 구현)

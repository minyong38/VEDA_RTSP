# Architecture

> RTSP 시그널링을 직접 구현하고, RTP 전송은 라이브러리를 활용하는 구조.

## Overview

VEDA_RTSP는 RTSP 시그널링 프로토콜을 RFC 2326 기반으로 직접 구현한 스트리밍 서버다.
RTP 영상 전송은 검증된 라이브러리(live555 또는 GStreamer)를 활용하여 안정성을 확보한다.

## Component Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                       veda_rtsp                              │
│                                                              │
│   Client ──TCP 8554──▶  rtsp::Server ──▶ rtsp::Session      │
│   (VLC/ffplay)              │                │               │
│                             │                ▼               │
│                             │         rtsp::Parser           │
│                             │                │               │
│                             │                ▼               │
│                             │         rtsp::Response         │
│                             │                │               │
│                             ▼                ▼               │
│                      net::TcpSocket    sdp::Builder          │
│                                              │               │
│                                              ▼               │
│   ┌─────────────────────────────────────────────────────┐   │
│   │              RTP 라이브러리 (live555/GStreamer)       │   │
│   │                         │                            │   │
│   │                         ▼                            │   │
│   │                    UDP 5004 ────────────────────────────▶ Client
│   └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## 책임 분리

### 직접 구현 (RTSP 시그널링)

| Module            | 책임 |
|-------------------|------|
| `rtsp/server`     | TCP accept 루프, 클라이언트 연결 관리 |
| `rtsp/parser`     | RTSP 요청 라인 + 헤더 파싱 |
| `rtsp/response`   | RTSP 응답 메시지 생성 |
| `rtsp/session`    | 세션 상태머신 (INIT → READY → PLAYING) |
| `sdp/builder`     | DESCRIBE 응답용 SDP 본문 생성 |
| `net/tcp_socket`  | POSIX TCP 래퍼 |

### 라이브러리 활용 (RTP 전송)

| Module            | 책임 |
|-------------------|------|
| `rtp/streamer`    | RTP 라이브러리 어댑터 (live555/GStreamer) |

## 데이터 흐름

```
1. 클라이언트 → TCP 연결 → rtsp::Server
2. rtsp::Server → 새 Session 생성
3. 클라이언트 → OPTIONS 요청 → rtsp::Parser → rtsp::Session
4. rtsp::Session → OPTIONS 응답 → rtsp::Response → 클라이언트
5. 클라이언트 → DESCRIBE 요청 → rtsp::Session
6. rtsp::Session → sdp::Builder → SDP 생성 → 클라이언트
7. 클라이언트 → SETUP 요청 → 포트 협상 → 세션 ID 발급
8. 클라이언트 → PLAY 요청 → RTP 라이브러리 시작 트리거
9. RTP 라이브러리 → UDP로 영상 스트리밍 → 클라이언트
```

## 외부 라이브러리 의존성

| 구분 | 라이브러리 | 용도 |
|------|-----------|------|
| RTP 전송 | live555 또는 GStreamer | 영상 패킷화 및 UDP 전송 |
| 로깅 | spdlog (선택) | 디버그 로그 |
| 테스트 | GoogleTest | 단위 테스트 |

**RTSP 시그널링은 외부 라이브러리 없이 직접 구현** — 학습 목적.

## 참고 RFC

- RFC 2326 — RTSP 1.0 (직접 구현)
- RFC 4566 — SDP (직접 구현)
- RFC 3550 — RTP (라이브러리가 처리)
- RFC 6184 — H.264 RTP payload format (라이브러리가 처리)

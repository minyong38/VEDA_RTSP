# Architecture

> 채워나가는 문서. Week 1 끝나면 첫 번째 다이어그램부터.

## Overview

VEDA_RTSP는 H.264 영상을 RTSP/RTP 프로토콜로 전송하는 서버다.
의존성 없이 POSIX 소켓 API만 사용해 처음부터 구현한다.

## Component diagram

```
                  ┌─────────────────────────────────────┐
                  │            veda_rtsp                │
                  │                                     │
   Client ──TCP──▶│  rtsp::Server ──▶ rtsp::Session     │
   (ffplay/VLC)   │      │                │             │
                  │      │                ▼             │
                  │      │          sdp::Builder        │
                  │      │                              │
                  │      ▼                              │
                  │  net::TcpListener                   │
                  │                                     │
                  │                ┌─── rtp::Packetizer │
                  │                │           │        │
                  │                ▼           ▼        │
                  │      rtp::h264_fua    net::UdpSocket│──UDP──▶ Client
                  └─────────────────────────────────────┘
```

## Module responsibilities

| Module        | 책임 |
|---------------|------|
| `rtsp/server` | TCP accept 루프, Session 생성 |
| `rtsp/parser` | RTSP 요청 라인 + 헤더 파싱 |
| `rtsp/session`| 세션 상태머신 (INIT → READY → PLAYING) |
| `sdp/builder` | DESCRIBE 응답용 SDP 본문 생성 |
| `rtp/packetizer` | RTP 12바이트 헤더 작성 |
| `rtp/h264_fua` | NAL → FU-A 분할 (RFC 6184) |
| `net/tcp_socket` | POSIX TCP 래퍼 |
| `net/udp_socket` | POSIX UDP 래퍼 (RTP 전송) |

## 외부 라이브러리 의존성

**없음.** Live555 / GStreamer 대신 직접 구현 — 학습 목적.

## 참고 RFC

- RFC 2326 — RTSP 1.0
- RFC 3550 — RTP
- RFC 4566 — SDP
- RFC 6184 — H.264 RTP payload format

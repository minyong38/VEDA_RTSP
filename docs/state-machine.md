# RTSP Session State Machine

> RFC 2326 기반 RTSP 세션 상태 전이

## State Diagram

```
                    OPTIONS (any state)
                         │
       ┌─────────────────┼─────────────────┐
       │                 │                 │
       ▼                 ▼                 ▼
    ┌──────┐         ┌───────┐         ┌─────────┐
    │ INIT │         │ READY │         │ PLAYING │
    └──┬───┘         └───┬───┘         └────┬────┘
       │                 │                   │
       │    DESCRIBE     │                   │
       │ (SDP 응답)      │                   │
       │                 │                   │
       │    SETUP        │                   │
       ├────────────────►│                   │
       │  (세션 생성)    │                   │
       │                 │                   │
       │                 │      PLAY         │
       │                 ├──────────────────►│
       │                 │  (스트리밍 시작)  │
       │                 │                   │
       │                 │      PAUSE        │
       │                 │◄──────────────────┤
       │                 │  (스트리밍 중지)  │
       │                 │                   │
       │    TEARDOWN     │     TEARDOWN      │
       │        │        │         │         │
       │        ▼        │         ▼         │
       │     ┌───────────┴─────────┐         │
       └────►│       CLOSED        │◄────────┘
             └─────────────────────┘
```

## State Descriptions

| State | 설명 |
|-------|------|
| **INIT** | 초기 상태. TCP 연결 수립 직후 |
| **READY** | SETUP 완료. RTP 포트 협상됨, 스트리밍 대기 중 |
| **PLAYING** | PLAY 이후. RTP 스트리밍 진행 중 |
| **CLOSED** | TEARDOWN 이후. 세션 종료 |

## Transitions

| From | Method | To | 비고 |
|------|--------|-----|------|
| INIT | OPTIONS | INIT | 가능한 메서드 목록만 응답 |
| INIT | DESCRIBE | INIT | SDP 반환. 상태 변경 없음 |
| INIT | SETUP | READY | RTP 포트 협상, Session ID 부여 |
| READY | PLAY | PLAYING | RTP 스트리밍 시작 트리거 |
| PLAYING | PAUSE | READY | RTP 스트리밍 일시정지 |
| READY | TEARDOWN | CLOSED | 리소스 해제 |
| PLAYING | TEARDOWN | CLOSED | 리소스 해제 |

## 메서드별 처리

### OPTIONS
```
Client → Server: OPTIONS rtsp://server/stream RTSP/1.0
Server → Client: RTSP/1.0 200 OK
                 Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN
```
- 상태 변경 없음
- 서버가 지원하는 메서드 목록 반환

### DESCRIBE
```
Client → Server: DESCRIBE rtsp://server/stream RTSP/1.0
Server → Client: RTSP/1.0 200 OK
                 Content-Type: application/sdp

                 v=0
                 o=- 0 0 IN IP4 192.168.0.10
                 ...
```
- SDP 본문 반환
- 상태 변경 없음 (일부 구현에서는 READY로)

### SETUP
```
Client → Server: SETUP rtsp://server/stream/track1 RTSP/1.0
                 Transport: RTP/AVP;unicast;client_port=5000-5001
Server → Client: RTSP/1.0 200 OK
                 Session: 12345678
                 Transport: RTP/AVP;unicast;client_port=5000-5001;server_port=6000-6001
```
- 세션 ID 발급
- RTP/RTCP 포트 협상
- INIT → READY 전이

### PLAY
```
Client → Server: PLAY rtsp://server/stream RTSP/1.0
                 Session: 12345678
Server → Client: RTSP/1.0 200 OK
                 Session: 12345678
```
- RTP 라이브러리에 스트리밍 시작 요청
- READY → PLAYING 전이

### TEARDOWN
```
Client → Server: TEARDOWN rtsp://server/stream RTSP/1.0
                 Session: 12345678
Server → Client: RTSP/1.0 200 OK
```
- RTP 스트리밍 중지
- 세션 리소스 해제
- → CLOSED 전이

## 에러 처리

| 상황 | 응답 코드 |
|------|----------|
| 잘못된 상태에서의 메서드 호출 | `455 Method Not Valid in This State` |
| 알 수 없는 메서드 | `501 Not Implemented` |
| 파싱 실패 | `400 Bad Request` |
| 세션 ID 불일치 | `454 Session Not Found` |
| 지원하지 않는 Transport | `461 Unsupported Transport` |

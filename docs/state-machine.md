# RTSP Session State Machine

```
       OPTIONS              DESCRIBE
   ┌─────────────┐      ┌──────────────┐
   │             ▼      │              ▼
  INIT ─────────────────────────────────── READY
   │                                         │
   │                                  SETUP  │
   │                                         │
   │                                         ▼
   │                                       READY
   │                                         │
   │                              PLAY ◀────┤───▶ PAUSE
   │                                │        ▲
   │                                ▼        │
   │                              PLAYING ───┘
   │                                │
   │                       TEARDOWN │
   │                                ▼
   └──────────────────────────────CLOSED
```

## Transitions

| From    | Method    | To       | 비고 |
|---------|-----------|----------|------|
| INIT    | OPTIONS   | INIT     | 가능한 메서드 목록만 응답 |
| INIT    | DESCRIBE  | INIT/READY | SDP 반환. 일부 클라이언트는 여기서 READY로 간주 |
| INIT    | SETUP     | READY    | RTP 포트 협상, Session ID 부여 |
| READY   | PLAY      | PLAYING  | RTP 송출 시작 |
| PLAYING | PAUSE     | READY    | RTP 송출 일시정지 |
| READY   | TEARDOWN  | CLOSED   | 리소스 해제 |
| PLAYING | TEARDOWN  | CLOSED   | 리소스 해제 |

## 에러 처리

- 잘못된 상태에서의 메서드 호출 → `455 Method Not Valid in This State`
- 알 수 없는 메서드 → `501 Not Implemented`
- 파싱 실패 → `400 Bad Request`

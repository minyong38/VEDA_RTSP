# VEDA_RTSP — RTSP 프로토콜 직접 구현 (개인 기획안)

> **VEDA 아카데미 4기 사이드 프로젝트**
> 한화비전(Network Camera 펌웨어) 직무 포트폴리오용

---

## 1. 프로젝트 한 줄 정의

**RTSP 시그널링 프로토콜을 RFC 2326 표준 문서를 보고 C++로 직접 구현하고, RTP 영상 전송은 검증된 라이브러리를 활용하여 VLC/ffplay에서 영상을 재생할 수 있는 스트리밍 서버를 만든다.**

### 프로젝트 정체성

> *"RTSP 프로토콜의 시그널링 계층을 RFC 문서 수준에서 이해하고 직접 구현할 수 있는 네트워크 프로그래머."*

### 한화비전 포트폴리오 어필 포인트

| 한화비전이 보는 역량 | 본 프로젝트에서 증명 |
|---|---|
| Network Camera 펌웨어 개발 | **RTSP 시그널링을 RFC 수준에서 직접 구현** |
| IP 카메라 프로토콜 이해 | RTSP 상태머신, SDP 생성, 세션 관리 |
| 네트워크 프로그래밍 | POSIX 소켓 + epoll 기반 멀티클라이언트 |
| 시스템 통합 능력 | RTSP(직접 구현) + RTP(라이브러리) 연동 |

---

## 2. 시스템 아키텍처

```
┌─────────────────────────────────────────────────────────┐
│                    [외부 — Client]                       │
│              VLC, ffplay, Wisenet WAVE                  │
└────┬──────────────────────────┬─────────────────────────┘
     │                          │
     │ RTSP TCP 8554            │ RTP UDP 5004
     │ (시그널링)               │ (영상 데이터)
     │                          │
     ▼                          ▼
┌────────────────────────────────────────────────────────┐
│              내 코드 영역 (C++ 바이너리)                 │
│                                                         │
│  ┌──────────────────────┐   ┌────────────────────────┐ │
│  │  RTSP 서버 (직접 구현) │   │ RTP 전송 (라이브러리)  │ │
│  │  - 요청 파서          │   │ - live555 또는        │ │
│  │  - 응답 생성          │──▶│ - GStreamer           │ │
│  │  - 세션 상태머신      │   │                        │ │
│  │  - SDP 생성           │   │                        │ │
│  └──────────────────────┘   └────────────────────────┘ │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

**핵심 분리:**
- **RTSP 시그널링**: 직접 구현 (학습 목적, 포트폴리오 가치)
- **RTP 전송**: 라이브러리 사용 (안정성 확보, 시간 절약)

---

## 3. 핵심: RTSP 서버 직접 구현

### 3.1 구현 범위 (RFC 2326 기반)

| 메서드 | 구현 우선순위 | 비고 |
|---|---|---|
| `OPTIONS` | 필수 | 서버 capability 응답 |
| `DESCRIBE` | 필수 | SDP 반환 |
| `SETUP` | 필수 | RTP 포트 협상, 세션 ID 발급 |
| `PLAY` | 필수 | RTP 송신 시작 트리거 |
| `PAUSE` | 선택 | 송신 일시중지 |
| `TEARDOWN` | 필수 | 세션 정리 |
| `GET_PARAMETER` | 선택 | Keep-alive |
| `SET_PARAMETER` | 스트레치 | 동적 설정 변경 |

### 3.2 RTSP 요청 파서

```cpp
// 파싱해야 할 RTSP 요청 예시
// OPTIONS rtsp://192.168.0.10:8554/stream RTSP/1.0
// CSeq: 1
// User-Agent: VLC/3.0.20

struct RtspRequest {
    std::string method;      // "OPTIONS", "DESCRIBE", ...
    std::string uri;         // "rtsp://192.168.0.10:8554/stream"
    std::string version;     // "RTSP/1.0"
    std::unordered_map<std::string, std::string> headers;
    // CSeq, Transport, Session 등
};

class RtspParser {
public:
    std::optional<RtspRequest> parse(const std::string& raw);
private:
    bool parse_request_line(const std::string& line, RtspRequest& req);
    bool parse_header(const std::string& line, RtspRequest& req);
};
```

### 3.3 RTSP 응답 생성

```cpp
// 생성해야 할 RTSP 응답 예시
// RTSP/1.0 200 OK
// CSeq: 1
// Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN

class RtspResponse {
public:
    static std::string ok(int cseq, const std::string& body = "");
    static std::string options_response(int cseq);
    static std::string describe_response(int cseq, const std::string& sdp);
    static std::string setup_response(int cseq, const std::string& session_id,
                                       int server_rtp_port, int server_rtcp_port);
    static std::string play_response(int cseq, const std::string& session_id);
    static std::string error(int cseq, int status_code, const std::string& reason);
};
```

### 3.4 세션 상태머신

```
        ┌─────┐
        │ INIT│
        └──┬──┘
           │ SETUP
           ▼
        ┌─────┐  PLAY    ┌─────────┐
        │READY│ ───────► │ PLAYING │
        └──┬──┘ ◄─────── └────┬────┘
           │       PAUSE      │
           │ TEARDOWN         │ TEARDOWN
           ▼                  ▼
        ┌─────┐
        │ END │
        └─────┘
```

```cpp
class RtspSession {
    enum class State { INIT, READY, PLAYING, END };

    std::string session_id_;
    State state_ = State::INIT;

    // 클라이언트 RTP 수신 정보
    std::string client_ip_;
    int client_rtp_port_;
    int client_rtcp_port_;

public:
    std::string handle_options(const RtspRequest& req);
    std::string handle_describe(const RtspRequest& req);
    std::string handle_setup(const RtspRequest& req);
    std::string handle_play(const RtspRequest& req);
    std::string handle_teardown(const RtspRequest& req);

private:
    std::string generate_session_id();
};
```

### 3.5 SDP 생성 (Session Description Protocol)

```cpp
// DESCRIBE 응답에 포함될 SDP
class SdpBuilder {
public:
    std::string build(const std::string& server_ip, int rtp_port);
};

// 생성 예시:
// v=0
// o=- 0 0 IN IP4 192.168.0.10
// s=VEDA RTSP Stream
// c=IN IP4 0.0.0.0
// t=0 0
// a=control:*
// m=video 0 RTP/AVP 96
// a=rtpmap:96 H264/90000
// a=fmtp:96 packetization-mode=1
// a=control:track1
```

### 3.6 TCP 서버 (멀티클라이언트)

```cpp
class RtspServer {
    int listen_fd_;
    int epoll_fd_;
    std::unordered_map<int, std::unique_ptr<RtspSession>> sessions_;

public:
    bool start(int port);
    void run();  // epoll 이벤트 루프

private:
    void on_new_client(int client_fd);
    void on_client_data(int client_fd);
    void on_client_disconnect(int client_fd);
};
```

---

## 4. RTP 전송 — 라이브러리 활용

### 4.1 라이브러리 선택지

| 라이브러리 | 장점 | 단점 |
|---|---|---|
| **live555** | RTSP/RTP 전문, 가벼움 | C 스타일, 콜백 복잡 |
| **GStreamer** | 파이프라인 구성 쉬움 | 의존성 큼 |
| **FFmpeg libavformat** | 코덱 지원 풍부 | 너무 거대 |

### 4.2 연동 방식

RTSP 시그널링은 내가 직접 처리하고, `PLAY` 명령이 오면 RTP 라이브러리에 스트리밍 시작을 요청:

```cpp
// 내 RTSP 서버가 PLAY 요청을 받으면
void RtspSession::handle_play(const RtspRequest& req) {
    // 1. RTSP 응답 생성 (직접 구현)
    auto response = RtspResponse::play_response(cseq, session_id_);
    send_response(response);

    // 2. RTP 스트리밍 시작 (라이브러리 호출)
    rtp_streamer_->start(client_ip_, client_rtp_port_);
}
```

### 4.3 테스트용 영상 소스

```bash
# 테스트용 H.264 파일 생성
ffmpeg -f lavfi -i testsrc=duration=60:size=1280x720:rate=30 \
       -c:v libx264 -preset ultrafast -tune zerolatency \
       -f h264 test.h264
```

---

## 5. 기술 스택 & 의존성

### 사용할 것

| 영역 | 기술 |
|---|---|
| 언어 | **C++17** |
| 빌드 | CMake |
| RTSP 시그널링 | **POSIX 소켓 직접** (socket, bind, listen, accept, epoll) |
| RTP 전송 | live555 또는 GStreamer (라이브러리) |
| 로깅 | spdlog 또는 직접 작성 |
| 테스트 | GoogleTest |

### 의도적으로 배제한 것

| 회피 대상 | 이유 |
|---|---|
| **live555의 RTSP 서버** | "RTSP 직접 구현"의 의미가 없어짐 |
| **gst-rtsp-server** | GStreamer가 다 해버려서 학습 가치 ↓ |
| **Boost.Asio** | 면접관이 "Boost 없이 짤 수 있어요?" 물어볼 위험 |

→ 원칙: **"RTSP 시그널링은 표준 라이브러리만으로 직접 구현. RTP 전송만 라이브러리 활용."**

---

## 6. 8주 일정

| Week | 핵심 작업 | 산출물 |
|---|---|---|
| **1** | TCP 소켓 서버 골격, RTSP 요청 파서 | telnet으로 OPTIONS 응답 확인 |
| **2** | SDP 생성, DESCRIBE/SETUP 핸들러 | VLC가 SETUP 단계까지 진행 |
| **3** | PLAY/TEARDOWN, 세션 상태머신 완성 | 전체 RTSP 핸드셰이크 성공 |
| **4** | RTP 라이브러리 연동, 첫 영상 재생 | **VLC에서 영상 재생 성공** ⭐ |
| **5** | 멀티클라이언트 (epoll), 세션 관리 | 동시 3개 클라이언트 검증 |
| **6** | 에러 처리, 엣지 케이스 대응 | 안정성 확보 |
| **7** | 성능 측정, Wireshark 분석 | 측정 리포트 |
| **8** | 문서 정리, 데모 영상, 포트폴리오화 | **GitHub + 데모 영상** |

### Critical Path

```
Week 1-2: RTSP 파서가 안 되면 그 다음이 불가능
Week 3:   세션 상태머신이 핵심 마일스톤
Week 4:   ⭐ RTP 라이브러리 연동 + 첫 영상 재생
Week 8:   포트폴리오 자료화 시간 충분히 확보 (절대 양보 X)
```

---

## 7. 도전 과제 & 해결 방향

### Challenge 1. RTSP 파싱 정확도

**문제**: VLC, ffplay, Wisenet 등 클라이언트마다 요청 형식이 조금씩 다름.

**해결**:
- RFC 2326 스펙 기반으로 관대하게 파싱 (Postel's Law)
- Wireshark로 실제 클라이언트 요청 캡처해서 테스트 케이스 추가
- 단위 테스트 철저히

### Challenge 2. SDP 호환성

**문제**: SDP가 잘못되면 클라이언트가 디코더 준비를 못 함.

**해결**:
- 실제 카메라(또는 live555 데모 서버)의 SDP를 레퍼런스로 사용
- `profile-level-id`, `packetization-mode` 정확히 설정
- VLC의 상세 로그로 어디서 실패하는지 확인

### Challenge 3. 세션 관리

**문제**: 클라이언트가 비정상 종료하면 세션이 좀비로 남음.

**해결**:
- TCP 연결 끊김 감지 (epoll EPOLLHUP)
- 세션 타임아웃 (GET_PARAMETER 없으면 60초 후 정리)
- TEARDOWN 없이 끊겨도 리소스 정리

### Challenge 4. RTP 라이브러리 연동

**문제**: 내 RTSP 서버와 RTP 라이브러리 사이 인터페이스 설계.

**해결**:
- 추상 인터페이스로 분리 (`RtpStreamer` 클래스)
- live555와 GStreamer 둘 다 어댑터 패턴으로 연결 가능하게
- 처음에는 live555로 시작 (더 가벼움)

---

## 8. 산출물 & 어필 포인트

### GitHub Repository 구성

```
VEDA_RTSP/
├── README.md                   ← 영문/한글, 데모 GIF, 아키텍처 다이어그램
├── CMakeLists.txt
├── docs/
│   ├── architecture.md
│   ├── state-machine.md        ← RTSP 상태머신 상세
│   ├── rtsp-protocol.md        ← RFC 2326 요약 + 구현 매핑
│   └── benchmarks.md           ← 성능 측정 결과
├── src/
│   ├── main.cpp
│   ├── rtsp/                   ← RTSP 시그널링 (직접 구현)
│   │   ├── server.cpp
│   │   ├── session.cpp
│   │   ├── parser.cpp
│   │   └── response.cpp
│   ├── sdp/
│   │   └── builder.cpp
│   ├── rtp/                    ← RTP 라이브러리 어댑터
│   │   └── streamer.cpp
│   └── net/
│       └── tcp_socket.cpp
├── tests/
│   ├── test_parser.cpp
│   ├── test_session.cpp
│   └── test_sdp_builder.cpp
└── tools/
    └── rtsp_client.py          ← 테스트용 간이 클라이언트
```

### 포트폴리오 자료

| 자료 | 형태 | 어필 포인트 |
|---|---|---|
| 데모 영상 | 3분 mp4 | VLC + ffplay 동시 접속 |
| Wireshark 캡처 | .pcap | RTSP 핸드셰이크 시퀀스 시각화 |
| 아키텍처 다이어그램 | PNG/SVG | RTSP(직접) + RTP(라이브러리) 분리 |
| 코드 분량 | LOC 통계 | "RTSP 시그널링 ~1500 LOC C++로 구현" |

### 면접에서 받을 질문 대비

1. **"RTSP와 RTP 차이가 뭡니까?"**
   → RTSP는 시그널링(제어), RTP는 데이터 전송. RTSP는 TCP 텍스트 프로토콜, RTP는 UDP 바이너리.

2. **"왜 RTSP만 직접 구현하고 RTP는 라이브러리를 썼습니까?"**
   → RTSP 시그널링이 프로토콜 이해의 핵심이고, RTP 패킷화는 이미 잘 검증된 라이브러리가 있어서 효율적으로 시간 배분했습니다. 시그널링 계층을 깊이 이해하면 RTP 쪽은 필요시 확장 가능합니다.

3. **"RTSP 상태머신이 뭡니까?"**
   → INIT → READY → PLAYING → END. SETUP으로 세션 생성, PLAY로 스트리밍 시작, TEARDOWN으로 정리.

4. **"멀티클라이언트는 어떻게 처리했습니까?"**
   → epoll 기반 단일 스레드 이벤트 루프. 세션별 객체로 상태 격리.

5. **"SDP가 뭡니까?"**
   → Session Description Protocol. 미디어 스트림의 코덱, 포트, 포맷 정보를 텍스트로 기술. DESCRIBE 응답에 포함됨.

---

## 9. 최종 한 줄 어필

> *"RTSP 시그널링 프로토콜을 RFC 2326 표준 문서를 보고 C++로 직접 구현했습니다. 요청 파서, 응답 생성기, 세션 상태머신, SDP 생성을 모두 직접 작성하고, epoll 기반 멀티클라이언트를 지원합니다. RTP 전송은 live555를 활용하여 VLC/ffplay에서 영상 재생을 검증했습니다."*

---

## 부록 A. 참고 RFC 문서

| RFC | 제목 | 용도 |
|---|---|---|
| **RFC 2326** | Real Time Streaming Protocol (RTSP) | **핵심 — 직접 구현 대상** |
| **RFC 4566** | SDP: Session Description Protocol | SDP 문법 |
| RFC 3550 | RTP | 라이브러리가 처리 (참고용) |
| RFC 6184 | RTP Payload Format for H.264 | 라이브러리가 처리 (참고용) |
| RFC 7826 | RTSP 2.0 | 참고만, 구현은 1.0 |

## 부록 B. 주기적 체크포인트

- **Week 3 끝**: RTSP 핸드셰이크 전체 성공 (VLC가 PLAYING 상태까지)
- **Week 4 끝**: VLC에서 영상 재생. 안 되면 일정 재조정.
- **Week 8 끝**: 포트폴리오 자료 완성. 이 시간은 절대 양보 X.

---

*마지막 수정: 2025-05-24*

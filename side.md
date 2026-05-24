# EdgeTrack PTZ — RTSP/RTP 스택 직접 구현 (개인 기획안)

> **VEDA 아카데미 4기 사이드 프로젝트 — 본인 파트 한정**
> 한화비전(Network Camera 펌웨어) 직무 포트폴리오용

---

## 1. 프로젝트 한 줄 정의

**라즈베리파이 위에서 동작하는 IP 카메라의 영상 전송 스택(RTSP + RTP)을 C++로 RFC 표준 문서를 보고 직접 구현하고, 한화비전 자체 표준(SUNAPI) 호환 인터페이스를 얹은 펌웨어 모듈을 만든다.**

### 본인 파트 한 줄 정체성

> *"카메라 영상이 외부 VMS·VLC에 도달하기까지의 모든 네트워크 스택과, 외부 제어 명령이 STM32 모터까지 도달하기까지의 모든 통신 계층을 책임지는 펌웨어 엔지니어."*

### 한화비전 포트폴리오 어필 포인트

| 한화비전이 보는 역량 | 본 파트에서 증명 |
|---|---|
| Network Camera 펌웨어 개발 | **RTSP/RTP 스택을 RFC 수준에서 직접 구현** |
| IP 카메라 영상 전송 이해 | H.264 NAL 유닛 단편화(FU-A) 직접 처리 |
| 자사 API 친숙도 | SUNAPI 호환 CGI 서버 구현 |
| 시스템 통합(SoC ↔ MCU) | RPi ↔ STM32 UART 프로토콜 설계·구현 |
| 임베디드 네트워킹 | POSIX 소켓 + epoll 기반 멀티클라이언트 |

---

## 2. 내 모듈이 전체 시스템에서 차지하는 위치

```
┌─────────────────────────────────────────────────────────┐
│                    [외부 — VMS / Client]                 │
│    VLC, Wisenet WAVE, ONVIF Device Manager, 자체 클라이언트│
└────┬──────────────┬─────────────────────┬───────────────┘
     │              │                     │
     │ HTTP 80      │ RTSP TCP 8554       │ RTP UDP 5004
     │ (SUNAPI)     │ (시그널링)          │ (영상 데이터)
     ▼              ▼                     ▲
┌────────────────────────────────────────┴───────────────┐
│         라즈베리파이 — 내 코드 영역 (C++ 단일 바이너리)│
│                                                          │
│  ┌──────────────┐   ┌────────────────┐  ┌────────────┐ │
│  │ SUNAPI HTTP  │   │  RTSP 서버     │  │ RTP 패킷화 │ │
│  │ 서버          │   │  (시그널링)    │──│ + UDP 송신 │ │
│  │ /stw-cgi/*   │   │  상태머신       │  │ (RFC 6184) │ │
│  └──────┬───────┘   └────────┬───────┘  └─────▲──────┘ │
│         │                     │                 │       │
│         │                     │                 │       │
│         └─────────┬───────────┘                 │       │
│                   ▼                             │       │
│         ┌─────────────────┐         ┌───────────┴────┐  │
│         │ UART 송신 모듈  │         │ H.264 NAL 큐  │  │
│         └────────┬────────┘         └───────▲────────┘  │
│                  │                          │           │
└──────────────────┼──────────────────────────┼───────────┘
                   │ UART 115200              │
                   ▼                          │
            ┌─────────────┐         ┌─────────┴──────────┐
            │  STM32      │         │ B 담당: 카메라/ISP │
            │ (C/D 담당)  │         │ + GStreamer 인코더 │
            └─────────────┘         └────────────────────┘
```

**내가 만들 것 = 위 다이어그램의 가운데 박스(라즈베리파이 C++ 바이너리) 전체**

**입력**:
- B의 GStreamer 파이프라인이 뱉는 H.264 NAL 유닛 스트림 (shared memory or named pipe)
- B의 추적 로직이 만든 PTZ 목표각 (Δpan, Δtilt)
- 외부 클라이언트의 RTSP / SUNAPI 명령

**출력**:
- RTP 패킷 (UDP, 외부 VMS로)
- RTSP 응답 (TCP, 외부 클라이언트로)
- SUNAPI JSON 응답 (HTTP, 외부 클라이언트로)
- UART 패킷 (STM32로)

---

## 3. 핵심: RTSP/RTP 스택 직접 구현

### 3.1 RTSP 서버 (시그널링 계층)

**구현 범위 (Level 2 — RFC 2326 기반):**

| 메서드 | 구현 우선순위 | 비고 |
|---|---|---|
| `OPTIONS` | 필수 | 서버 capability 응답 |
| `DESCRIBE` | 필수 | SDP 반환 |
| `SETUP` | 필수 | UDP 포트 할당, 세션 ID 발급 |
| `PLAY` | 필수 | RTP 송신 시작 |
| `PAUSE` | 선택 | 송신 일시중지 |
| `TEARDOWN` | 필수 | 세션 정리 |
| `GET_PARAMETER` | 선택 | Keep-alive |
| `SET_PARAMETER` | 스트레치 | 동적 설정 변경 |

**상태 머신:**

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

**모듈 구조 (C++):**

```cpp
class RtspSession {
    std::string session_id_;
    State state_ = INIT;
    sockaddr_in client_rtp_addr_;
    int client_rtp_port_;
    uint16_t rtp_sequence_ = 0;
    uint32_t rtp_timestamp_ = 0;

public:
    std::string handle_options(const RtspRequest&);
    std::string handle_describe(const RtspRequest&);
    std::string handle_setup(const RtspRequest&);
    std::string handle_play(const RtspRequest&);
    std::string handle_teardown(const RtspRequest&);
};

class RtspServer {
    int listen_fd_;                              // TCP 8554
    std::unordered_map<int, RtspSession> sessions_;
    
public:
    void run();              // epoll 루프
    void on_new_client(int fd);
    void on_client_data(int fd);
};
```

### 3.2 SDP(Session Description Protocol) 생성

`DESCRIBE` 응답에 박을 SDP 텍스트를 동적으로 생성. **이게 잘못되면 VLC가 디코더 준비를 못 함.**

생성할 SDP 예시:

```
v=0
o=- 0 0 IN IP4 192.168.0.10
s=EdgeTrack PTZ
c=IN IP4 0.0.0.0
t=0 0
a=control:*
m=video 0 RTP/AVP 96
a=rtpmap:96 H264/90000
a=fmtp:96 packetization-mode=1;profile-level-id=42e01f;sprop-parameter-sets=Z0LAH9oBQBboQAAAAwBAAAAMo8WLkg==,aM48gA==
a=control:track1
```

**주의 포인트:**
- `sprop-parameter-sets` 에는 SPS/PPS를 Base64로 인코딩해서 박아야 함
- `profile-level-id` 는 인코더의 실제 프로파일과 일치해야 함
- `packetization-mode=1` 이어야 FU-A 분할 사용 가능

### 3.3 RTP 패킷화 (RFC 6184 — H.264 페이로드 포맷)

**가장 까다로운 부분. 핵심 알고리즘:**

```cpp
void RtpPacketizer::packetize(const uint8_t* nal_unit, size_t size) {
    // RTP/UDP MTU 보통 1400으로 잡음 (이더넷 MTU 1500 - IP/UDP/RTP 헤더 여유)
    constexpr size_t MAX_PAYLOAD = 1400 - 12;  // RTP 헤더 12바이트 제외
    
    if (size <= MAX_PAYLOAD) {
        // Single NAL Unit Mode — NAL 그대로 + RTP 헤더만
        send_single_nal(nal_unit, size);
    } else {
        // FU-A Fragmentation — NAL을 잘라서 여러 패킷으로
        send_fu_a(nal_unit, size);
    }
}

void RtpPacketizer::send_fu_a(const uint8_t* nal, size_t size) {
    uint8_t nal_header = nal[0];      // 첫 바이트 보관
    uint8_t nal_type = nal_header & 0x1F;
    uint8_t nri = nal_header & 0x60;
    
    size_t offset = 1;                 // 첫 바이트 건너뛰고 페이로드만 자름
    bool first = true;
    
    while (offset < size) {
        size_t chunk = std::min(MAX_PAYLOAD - 2, size - offset);
        bool last = (offset + chunk == size);
        
        uint8_t fu_indicator = nri | 28;     // FU-A type = 28
        uint8_t fu_header = (first << 7) | (last << 6) | nal_type;
        //                  Start bit         End bit       NAL type
        
        send_rtp_packet(fu_indicator, fu_header, nal + offset, chunk, last);
        offset += chunk;
        first = false;
    }
}
```

**RTP 헤더 구성 (12 바이트):**

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT      |       sequence number         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           synchronization source (SSRC) identifier            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- **V** = 2 (RTP 버전 고정)
- **PT** = 96 (Dynamic, H.264용 — SDP에서 합의)
- **Sequence Number**: 매 패킷마다 +1 (16비트 wrap)
- **Timestamp**: 90kHz clock 기준, 같은 프레임은 같은 timestamp
- **M (Marker)**: 한 프레임의 마지막 패킷에만 1
- **SSRC**: 세션 시작 시 랜덤 32비트

### 3.4 RTP/UDP 전송

```cpp
class RtpSender {
    int udp_fd_;
    sockaddr_in dest_;
    uint32_t ssrc_;
    uint16_t seq_ = 0;
    
public:
    void send_packet(const uint8_t* payload, size_t size, 
                     uint32_t timestamp, bool marker);
};
```

- 클라이언트별로 별도 UDP 소켓 (또는 destination address만 다르게)
- `sendto()` 직접 호출
- 멀티캐스트는 스트레치 목표

### 3.5 H.264 NAL 유닛 수집

B의 GStreamer 파이프라인과의 인터페이스:

```bash
# B의 GStreamer: H.264 인코딩 → fdsink로 stdout 송출
gst-launch-1.0 libcamerasrc ! videoconvert ! v4l2h264enc \
    ! h264parse ! video/x-h264,stream-format=byte-stream \
    ! fdsink fd=1 | ./my_rtsp_server
```

내 코드 측:

```cpp
// stdin에서 H.264 byte-stream 읽고 NAL 유닛 단위로 자름
class NalReader {
    std::vector<uint8_t> buffer_;
    
public:
    // 00 00 00 01 시작 코드로 NAL 경계 탐지
    std::optional<std::vector<uint8_t>> next_nal();
};
```

대안: `named pipe` 또는 `shared memory`(POSIX shm) 사용 — 더 깔끔하면 이쪽으로.

---

## 4. SUNAPI 호환 HTTP 서버 (서브 파트)

### 구현 범위 (한화 CGI 구조 모방)

```
stw-cgi/
├── system.cgi       ?msubmenu=deviceinfo&action=view     [구현]
├── system.cgi       ?msubmenu=network&action=view        [구현]
├── attributes.cgi                                         [구현]
├── ptzcontrol.cgi   ?msubmenu=continuous&action=control  [구현 — UART로 포워딩]
├── ptzcontrol.cgi   ?msubmenu=stop&action=control        [구현]
├── image.cgi        ?msubmenu=focus&action=view          [구현 — 더미 응답]
└── eventsources.cgi ?msubmenu=videoanalysis              [스트레치]
```

### 실제 응답 예시

```http
GET /stw-cgi/system.cgi?msubmenu=deviceinfo&action=view HTTP/1.1

→ 응답:
HTTP/1.1 200 OK
Content-Type: application/json

{
  "Model": "EDGETRACK-PTZ-01",
  "DeviceName": "VEDA EdgeTrack",
  "SerialNumber": "VEDA-2025-0001",
  "FirmwareVersion": "1.0.0",
  "Manufacturer": "VEDA Academy"
}
```

### 라이브러리 선택

- **mongoose** (단일 헤더 C 라이브러리) 또는
- **cpp-httplib** (단일 헤더 C++)

→ 한화 SUNAPI 명령 1개 처리 = HTTP 라우터 1개 함수. **하루에 5~6개 엔드포인트 처리 가능.**

### PTZ 제어 명령 흐름

```
[클라이언트] 
   │ GET /stw-cgi/ptzcontrol.cgi?msubmenu=continuous
   │     &action=control&Pan=10&Tilt=0
   ▼
[내 HTTP 서버]
   │ 쿼리 파라미터 파싱 → (pan=+10, tilt=0)
   ▼
[UART 송신 모듈]
   │ 패킷화: [STX][CMD=01][PAN_h=00][PAN_l=0A][TILT_h=00][TILT_l=00][CRC][ETX]
   ▼
[STM32] → 모터 회전
```

---

## 5. UART 통신 모듈 (STM32와의 브리지)

### 통신 사양

- **물리 계층**: UART, 115200 8N1
- **연결**: RPi GPIO 14/15 (UART0) ↔ STM32 USART
- **레벨 시프터**: 필요 시 추가 (RPi 3.3V ↔ STM32 3.3V는 직결 가능)

### 패킷 포맷 (STM32 팀과 합의 후 확정)

```
바이트 위치:  0    1     2-3        4-5       6      7
            STX  CMD   PAN각도    TILT각도   CRC8   ETX
            0x02 0x01  int16_t    int16_t   1byte  0x03

각도 인코딩: 0.1° 단위 정수 (예: 90.5° → 905)
범위:       PAN ±170° → ±1700,  TILT ±60° → ±600
```

### 명령 종류

| CMD | 의미 | 파라미터 |
|---|---|---|
| `0x01` | 절대 위치 이동 | Pan, Tilt (각도) |
| `0x02` | 상대 이동 (Continuous) | Pan, Tilt (속도) |
| `0x03` | 정지 | 없음 |
| `0x04` | 홈 포지션 | 없음 |
| `0x10` | ACK | 마지막 명령 ID |
| `0x11` | NACK | 에러 코드 |

### C++ 구현

```cpp
class UartBridge {
    int fd_;                                  // /dev/serial0
    std::thread reader_thread_;
    std::queue<UartPacket> rx_queue_;
    
public:
    bool open(const char* device, int baud);
    void send_ptz_absolute(float pan_deg, float tilt_deg);
    void send_ptz_continuous(float pan_speed, float tilt_speed);
    void send_stop();
    
private:
    uint8_t crc8(const uint8_t* data, size_t len);
    void reader_loop();                       // RX 인터럽트 대신 thread
};
```

- **재전송**: ACK 200ms 안에 안 오면 1회 재전송
- **에러 처리**: 3회 실패 시 상위로 에러 전달

---

## 6. 기술 스택 & 의존성 원칙

### 사용할 것

| 영역 | 기술 |
|---|---|
| 언어 | **C++17** (구조화된 바인딩, std::optional, std::string_view) |
| 빌드 | CMake |
| 네트워크 | **POSIX 소켓 직접** (`socket`, `bind`, `listen`, `accept`, `epoll`) |
| H.264 인코딩 | GStreamer 파이프라인 (외부 프로세스로 호출, B가 담당) |
| HTTP 서버 | cpp-httplib (단일 헤더, SUNAPI용에만 사용) |
| JSON | nlohmann/json (단일 헤더) |
| 로깅 | spdlog 또는 직접 작성 |
| 테스트 | GoogleTest |

### 사용하지 않을 것 (의도적 배제)

| 회피 라이브러리 | 이유 |
|---|---|
| **Live555** | "RTSP 직접 구현"의 의미가 없어짐 |
| **gst-rtsp-server** | GStreamer가 다 해버려서 학습 가치 ↓ |
| **FFmpeg libavformat** | 너무 거대함, 어필 약화 |
| **Boost.Asio** | 멋있긴 한데 면접관이 "Boost 빼고 짤 수 있어요?" 물어볼 위험 |

→ 원칙: **"RTSP/RTP는 표준 라이브러리만으로 구현. 외부 의존성 최소화."**
→ 면접에서 *"의존성을 어디까지 줄였습니까?"* 답할 때 명확함.

---

## 7. 8주 일정

| Week | 핵심 작업 | 산출물 |
|---|---|---|
| **1** | 환경 구축, TCP 소켓 서버 골격, RTSP 명령 파서 (`OPTIONS`, `DESCRIBE` 응답) | telnet으로 OPTIONS 응답 확인 |
| **2** | SDP 생성 로직, `SETUP` 핸들러, 세션 ID 발급, 상태 머신 | VLC가 SETUP 단계까지 진행 |
| **3** | RTP 헤더 구조체, Single NAL 모드 패킷화, `PLAY` 핸들러, UDP 송신 | **VLC에서 영상 첫 재생 성공** ⭐ |
| **4** | FU-A 분할, SPS/PPS 처리, IDR 동기화, 타임스탬프 정확도 | 1080p 30fps 끊김 없이 재생 |
| **5** | 멀티클라이언트(epoll), 세션 정리, `TEARDOWN`, Keep-alive | 동시 3개 클라이언트 검증 |
| **6** | SUNAPI HTTP 서버 + 5~6개 엔드포인트 + 한화 CGI 구조 모방 | curl로 모든 엔드포인트 검증 |
| **7** | UART 모듈 + STM32 연동, ptzcontrol.cgi → STM32 모터 회전 통합 | End-to-End: 브라우저에서 명령 → 모터 회전 |
| **8** | Wireshark 검증, 지연시간 측정, 데모 영상, 포트폴리오 정리 | **데모 영상 + 측정 리포트 + GitHub** |

### Critical Path

```
Week 1-2: RTSP 시그널링이 안 되면 그 다음이 불가능
Week 3:   ⭐ PLAY 응답 + RTP 첫 패킷 송신이 가장 큰 마일스톤
Week 4:   FU-A는 디버깅 지옥 가능성 있음 — Wireshark 필수
Week 5-7: 비교적 안정 단계, 통합 위주
Week 8:   포트폴리오 자료화 시간 충분히 확보 (절대 양보 X)
```

---

## 8. 도전 과제 & 해결 방향

### Challenge 1. RTP 패킷화 디버깅 — Wireshark 의존

**문제**: NAL 유닛을 잘못 자르면 VLC가 디코드 실패하거나 화면이 깨짐. 에러 메시지 없음.

**해결**:
- Wireshark의 **RTP 분석 필터** 활용: `rtp.p_type == 96`
- 기존 IP 카메라(또는 Live555 데모 서버)와 패킷 덤프 나란히 비교
- 단편화된 NAL 유닛 재조립이 정확한지 패킷 단위 검증
- `gst-launch` 의 `rtph264depay` 디버그 로그로 어디서 깨지는지 확인

### Challenge 2. End-to-End 지연시간 300ms 이내

**문제**: 캡처 → 인코딩 → RTP → 네트워크 → 디코딩 누적 시 1초 넘기 쉬움.

**해결**:
- GStreamer 인코더 옵션: `tune=zerolatency`, `speed-preset=ultrafast`, **B-frame 비활성**
- GOP 크기 30 → 15
- VLC 클라이언트 측 `--network-caching=100`
- 측정 방법: 카메라 앞에 **밀리초 표시 타이머**(스마트폰)를 비추고 화면 캡처 동시 시점 비교
- 목표: **300ms 이하**, 측정값 리포트 자료화

### Challenge 3. RTP 타임스탬프 정확도

**문제**: 타임스탬프가 들쭉날쭉하면 VLC가 프레임을 떨어뜨리거나 버퍼링.

**해결**:
- 90kHz 클록 기준 일관성 유지 (`30fps` → 프레임당 3000)
- 같은 프레임(IDR + SPS + PPS + slice)은 **동일 timestamp** 사용
- 첫 프레임 시작 시점을 `random uint32_t`로 초기화 (RFC 권장)

### Challenge 4. 멀티클라이언트 동시 접속

**문제**: 클라이언트 1개일 땐 잘 되지만 2개 붙으면 한쪽이 끊김.

**해결**:
- `epoll` 기반 단일 스레드 이벤트 루프
- 클라이언트별 `RtspSession` 객체 분리 (`session_id` → 세션 매핑)
- RTP 송신은 각 세션의 `client_rtp_addr`로 `sendto()` — 데이터 복사 비용 한 번만

### Challenge 5. VLC와 Wisenet WAVE의 호환성 차이

**문제**: VLC에선 잘 되는데 Wisenet WAVE에선 영상이 안 뜸.

**해결**:
- 실제 한화 정품 카메라(있다면)의 RTSP/RTP 트래픽을 Wireshark로 캡처해 비교
- WAVE는 ONVIF 우선 디스커버리를 시도할 수 있어, **SUNAPI 응답으로 카메라 자기 ID 정확히 표시**
- SDP의 `profile-level-id`, `packetization-mode` 정확히 일치시키기

### Challenge 6. SUNAPI 정식 문서 부재

**문제**: SUNAPI 전체 명세는 한화 영업 통해야 받을 수 있음.

**해결**:
- 공개된 부분(Hanwha Support Portal 글, Wisenet Device Manager CGI Sender)을 역공학
- 한화 STEP Open Platform(`step.hanwhavision.com`)의 공개 SDK 분석
- "공개된 정보만으로 핵심 5~6개 엔드포인트를 클론 구현" — 이 자체가 어필 포인트

### Challenge 7. STM32 통신 안정성

**문제**: UART 패킷 손실 시 모터 동작 이상.

**해결**:
- STX/ETX + CRC8 + 패킷 길이 필드
- ACK 200ms 타임아웃 후 1회 재전송
- 명령 ID(시퀀스 번호)로 중복 명령 방지

---

## 9. 산출물 & 어필 포인트

### GitHub Repository 구성

```
edgetrack-rtsp/
├── README.md                   ← 영문/한글, 데모 GIF, 아키텍처 다이어그램
├── CMakeLists.txt
├── docs/
│   ├── architecture.md
│   ├── rtsp_state_machine.md
│   ├── rtp_packetization.md   ← FU-A 알고리즘 설명
│   ├── sunapi_endpoints.md
│   └── benchmarks.md           ← 지연시간 측정 결과
├── src/
│   ├── rtsp/                   ← RTSP 시그널링
│   │   ├── server.cpp
│   │   ├── session.cpp
│   │   ├── sdp_builder.cpp
│   │   └── request_parser.cpp
│   ├── rtp/                    ← RTP 패킷화
│   │   ├── packetizer.cpp
│   │   ├── h264_nal_reader.cpp
│   │   └── udp_sender.cpp
│   ├── sunapi/                 ← SUNAPI HTTP 서버
│   │   └── cgi_handlers.cpp
│   ├── uart/                   ← STM32 통신
│   │   └── bridge.cpp
│   └── main.cpp
├── tests/                      ← GoogleTest
│   ├── test_rtp_packetizer.cpp
│   ├── test_sdp_builder.cpp
│   └── test_uart_protocol.cpp
└── tools/
    ├── rtsp_client.py          ← 테스트용 자체 클라이언트
    └── latency_measure.py      ← 지연시간 자동 측정
```

### 포트폴리오 자료

| 자료 | 형태 | 어필 포인트 |
|---|---|---|
| 데모 영상 | 3~5분 mp4 | VLC + Wisenet WAVE 동시 접속, 한 화면에 띄움 |
| Wireshark 캡처 | .pcap | RTSP 시퀀스 + RTP 패킷 흐름 시각화 |
| 지연시간 그래프 | 측정 리포트 | "평균 247ms, p99 312ms" 같은 정량 데이터 |
| 아키텍처 다이어그램 | PNG/SVG | 1장으로 모든 모듈 표현 |
| 코드 분량 | LOC 통계 | "라이브러리 의존 없이 ~3000 LOC C++로 구현" |

### 면접에서 받을 질문 대비 (예상)

1. **"RTSP와 RTP 차이가 뭡니까?"**
   → RTSP는 시그널링, RTP는 데이터. RTSP는 TCP 텍스트, RTP는 UDP 바이너리.

2. **"왜 Live555를 안 썼습니까?"**
   → 펌웨어 엔지니어 직무에 지원하면서 네트워크 영상 전송의 핵심을 라이브러리로만 처리하면 학습 가치가 떨어진다고 판단. RFC 표준 문서를 직접 보고 구현해서 디버깅 능력과 프로토콜 이해도를 확보하는 것이 목표였음.

3. **"FU-A가 뭡니까?"**
   → H.264 NAL 유닛이 MTU를 초과할 때 여러 RTP 패킷으로 분할하는 RFC 6184 모드. Start/End 비트로 재조립 가능.

4. **"멀티클라이언트는 어떻게 처리했습니까?"**
   → epoll 기반 단일 스레드 이벤트 루프. 세션별 객체로 상태 격리.

5. **"한화 SUNAPI를 안 거치고 ONVIF만 했으면 어땠을까요?"**
   → SUNAPI가 한화 자체 표준이라 자사 제품에 대한 도메인 이해도를 직접 보여줄 수 있다고 판단. ONVIF는 RTSP/RTP 위에 SOAP 시그널링 한 층 더 얹는 구조라, RTSP를 깊이 다룬 경험이 ONVIF 구현으로 자연스럽게 확장 가능하다고 봤음.

---

## 10. 최종 한 줄 어필

> *"라즈베리파이 위에서 동작하는 IP 카메라의 **RTSP/RTP 영상 전송 스택을 RFC 2326/3550/6184 표준 문서를 보고 C++로 처음부터 구현**했습니다. H.264 NAL 유닛의 FU-A 단편화까지 직접 처리하며 1080p 30fps를 300ms 이하 지연으로 송출했고, **한화비전의 자체 표준인 SUNAPI 호환 인터페이스**를 함께 구현해 자사 VMS인 Wisenet WAVE와의 연동도 검증했습니다."*

---

## 부록 A. 참고할 RFC 문서

| RFC | 제목 | 용도 |
|---|---|---|
| RFC 2326 | Real Time Streaming Protocol (RTSP) | RTSP 시그널링 전체 |
| RFC 3550 | RTP: A Transport Protocol for Real-Time Applications | RTP 헤더 구조 |
| RFC 3551 | RTP Profile for Audio and Video Conferences | Payload Type 정의 |
| RFC 6184 | RTP Payload Format for H.264 Video | **FU-A 단편화** |
| RFC 4566 | SDP: Session Description Protocol | SDP 문법 |
| RFC 7826 | RTSP 2.0 | (참고만, 구현은 1.0) |

## 부록 B. 진행 중 주기적 체크포인트

- **Week 3 끝**: VLC에서 영상이 한 프레임이라도 떠야 함. 안 뜨면 일정 재조정.
- **Week 5 끝**: 멀티클라이언트 검증. 안정성 부족하면 SUNAPI 스코프 축소.
- **Week 7 끝**: 전체 통합 데모 완성. Week 8은 자료화 전용.

---

*마지막 수정: 2026-05-20*
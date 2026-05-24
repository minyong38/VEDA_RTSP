# 한화비전 SUNAPI 공식 자료 정리

> 출처: 한화비전 공식 교육자료 (SUNAPI와 외부연동 섹션)
> 정리 목적: VEDA 4기 EdgeTrack PTZ 사이드 프로젝트의 SUNAPI 호환 HTTP 서버 구현 레퍼런스
> 정리일: 2026-05-23

---

## 1. SUNAPI란?

**SUNAPI = Smart Unified Network API**

- 한화비전의 다양한 네트워크 장비를 **제어/설정**하는 공식 API
- **HTTP URL 형식**으로 동작 (REST 스타일)
- 카메라, NVR, 저장장치, VMS(SSM) 간 통합 연동의 표준

### 관련 용어 정리

| 용어 | 정의 |
|---|---|
| API | 응용프로그램 프로그래밍 인터페이스. 컴퓨터/프로그램 간의 연결, 다른 SW에 서비스 제공 |
| SDK | 소프트웨어 개발 키트. API/IDE/문서/라이브러리/샘플코드 포함. **API는 통상적으로 SDK의 컨텐츠 중 하나** |
| CGI | Common Gateway Interface. 웹 서버에서 동적 페이지를 보여주기 위해 임의의 프로그램을 실행할 수 있도록 하는 기술 |

→ **SUNAPI는 CGI 기반의 HTTP API**라는 점이 핵심. 한화 카메라 내부에 작은 웹 서버가 돌고 있고, 거기에 HTTP 요청을 보내는 방식.

---

## 2. 시스템 구성과 연동 방식

```
[카메라] ──SUNAPI──> [카메라] ──SUNAPI──> [저장장치/NVR] ──SUNAPI──> [SSM (VMS)]
   │                    │                    │                          ▲
   │ RTSP/ONVIF         │ SUNAPI/RTSP/ONVIF  │ SUNAPI/RTSP             │ HTTP API
   ▼                    ▼                    ▼                          │
[타사 저장장치]    [타사 프로그램 + SSM SDK]                       [타사 카메라]
                                                                  RTSP/ONVIF
```

### 연동 프로토콜 매트릭스

| 대상 | 한화 → 한화 | 한화 → 타사 | 타사 → 한화 |
|---|---|---|---|
| 카메라 ↔ 저장장치 | **SUNAPI** | RTSP/ONVIF | RTSP/ONVIF |
| 저장장치 ↔ VMS | **SUNAPI** | HTTP API | HTTP API |
| 카메라 ↔ VMS | **SUNAPI** | SSM SDK | RTSP/ONVIF |

→ **핵심 인사이트**: 한화 장비끼리는 SUNAPI로 다 통하고, 타사 장비와의 연동에만 RTSP/ONVIF가 필요하다. **SUNAPI는 한화의 "내부 통신 표준"**.

---

## 3. SUNAPI 명령 구조 (URL 포맷)

### 기본 형태

```
http://<Device IP>/stw-cgi/<value>.cgi?msubmenu=<value>&action=<value>[&<parameter>=<value>]
     └─ 장치 IP ──┘└─ 고정 ─┘└─ CGI 이름 ─┘└── 서브메뉴 ──┘└─ 액션 ─┘└── 파라미터 ──┘
```

### 구성 요소

| 요소 | 설명 | 예시 |
|---|---|---|
| `/stw-cgi/` | **고정 경로** (한화 SUNAPI의 root) | `/stw-cgi/` |
| CGI 이름 | 기능별 카테고리 | `system.cgi`, `ptzcontrol.cgi` |
| `msubmenu` | 서브메뉴 (세부 기능) | `deviceinfo`, `continuous` |
| `action` | 수행할 동작 | `view`, `set`, `update`, `add`, `control` |
| `parameter` | 선택적 보조 항목 | `Pan=10&Tilt=0` |

### Action 값 의미

- `view`: 현재의 설정을 응답으로 수신
- `set`: 파라미터의 내용을 서브메뉴에 설정
- `update`, `add`, `control` 등 다양한 액션 가능
- 커맨드에 따라 파라미터를 사용하지 않는 경우도 있어 **필수 항목은 아님**

---

## 4. SUNAPI 지원 범위 (CGI 목록)

| CGI 파일 | 기능 | EdgeTrack 구현 우선순위 |
|---|---|---|
| `system.cgi` | 시스템 설정 (기기 정보, 시간 등) | **★ 필수** |
| `network.cgi` | 네트워크 관리 | **★ 필수** |
| `security.cgi` | 보안 관리 | 선택 |
| `transer.cgi` | 서버 설정 | 선택 |
| `video.cgi` | 비디오/오디오 스트리밍 | 선택 |
| `image.cgi` | 이미지 조정 (포커스 등) | **★ 권장** |
| `ptzcontrol.cgi` / `ptzconfig.cgi` | **PTZ 제어** | **★★ 필수** |
| `eventsources.cgi` | 이벤트 모니터링 및 처리 (객체감지 포함) | **★ 권장** |
| `recording.cgi` | 저장된 데이터 검색 | 스킵 |
| `opensdk.cgi` | 오픈SDK 관리 | 스킵 |

→ EdgeTrack 본인 기획안의 6개 엔드포인트(system / network / attributes / ptzcontrol / image / eventsources)가 **실제 한화 SUNAPI 구조와 90% 일치**. 방향성 OK.

---

## 5. 실제 요청·응답 예시

### 요청 (URL)

```
GET http://192.168.1.100/stw-cgi/system.cgi?msubmenu=deviceinfo&action=view
```

### 응답 — 텍스트 형식

```
Model=XND-C9083RV
SerialNumber=ZR5K70GR700007A
FirmwareVersion=2.11.01_20211210_R251
BuildDate=2021.12.10
WebURL=http://www.hanwha-security.com/
DeviceType=NWC
ConnectedMACAddress=00:09:18:6B:71:D7
ISPVersion=1.00_211115
BootloaderVersion=
CGIVersion=2.5.9
ONVIFVersion=20.12
DeviceName=Camera
DeviceLocation=Location
DeviceDescription=Description
Memo=Memo
Language=English
PasswordStrength=Strong
OpenSDKVersion=4.01_210514
FirmwareGroup=XND-C9083RV
AIModelDetectionVersion=1.0_2.18_20211105_R0
```

### 응답 — JSON 형식

```json
{
  "Model": "XND-C9083RV",
  "SerialNumber": "ZR5K70GR700007A",
  "FirmwareVersion": "2.11.01_20211210_R251",
  "BuildDate": "2021.12.10",
  "WebURL": "http://www.hanwha-security.com/",
  "DeviceType": "NWC",
  "ConnectedMACAddress": "00:09:18:6B:71:D7",
  "ISPVersion": "1.00_211115",
  "BootloaderVersion": "",
  "CGIVersion": "2.5.9",
  "ONVIFVersion": "20.12",
  "DeviceName": "Camera",
  "DeviceLocation": "Location",
  "DeviceDescription": "Description",
  "Memo": "Memo",
  "Language": "English",
  "PasswordStrength": "Strong",
  "OpenSDKVersion": "4.01_210514",
  "FirmwareGroup": "XND-C9083RV",
  "AIModelDetectionVersion": "1.0_2.18_20211105_R0"
}
```

> **⚠️ EdgeTrack 본인 기획안 수정 포인트**: 본인 기획안에는 JSON 응답만 있었음. **실제 SUNAPI는 텍스트(key=value) / JSON 두 형식 모두 지원**. 두 형식 다 구현하면 한화 호환성 어필 강화.
>
> 응답 필드도 한화 실제 필드명 그대로 차용 권장: `Model`, `SerialNumber`, `FirmwareVersion`, `BuildDate`, `CGIVersion`, `ONVIFVersion`, `DeviceType=NWC`(Network Camera) 등.

---

## 6. SUNAPI 테스트 툴

| 도구 | 용도 | EdgeTrack 검증 시 |
|---|---|---|
| **웹브라우저** | URL 직접 입력해 GET 요청 테스트 | 1차 빠른 검증 |
| **디바이스 매니저** | 한화 공식 PC 툴. 한화비전 홈페이지 > 지원 > Tool > PC 설치용 Tool | **EdgeTrack에 실제 한화 툴이 붙는지 검증 → 포트폴리오 어필 ★** |
| **REST API 테스트 프로그램** | 예: **Postman** | 자동화 테스트 컬렉션 작성 |
| **OnCode** | 프로그램 코드 내에서 사용 | C++ 통합 테스트 |

### 실습 환경 구성

```
[카메라] ─────── 동일 네트워크 ─────── [PC]
                                       ├─ 웹브라우저
                                       └─ 디바이스 매니저
```

→ **EdgeTrack 시연 시나리오 추가**: "RPi에서 돌아가는 우리 SUNAPI 서버를 한화 공식 **디바이스 매니저**가 정상 인식하는 장면" — 이거 영상으로 캡쳐하면 면접관 임팩트 극대화.

---

## 7. SUNAPI의 실제 활용 사례 ★ EdgeTrack 핵심 인사이트

### 7.1 활용 영역

1. 당사 카메라/NVR 장비를 사용하는 **모니터링 프로그램 개발**
2. SI 시스템 또는 특정 장비에서 **CCTV 연동 및 일부 기능 제어**
3. **제품 내 기능 사용**
   - [카메라] **핸드오버**
   - [NVR] **이벤트 액션 (사용자 코딩)**

### 7.2 카메라 핸드오버 (Custom Query)

**기능**: 한 카메라가 추적 중인 객체를 **다른 카메라로 넘기는 기능**. 카메라 추가 화면에서 **Custom Query**로 SUNAPI 명령어를 직접 입력.

```
[카메라 A] ──핸드오버 명령──> [카메라 B]
   │  Custom Query (SUNAPI)
   ▼
"객체 발견! 옆 카메라야 받아라!"
```

**카메라 추가 UI**:
- 번호, IP타입(IPv4), **타입(HTTP)**, IP주소, 포트(80), 사용자(admin), 비밀번호
- **쿼리 스트링**: 핸드오버 기능으로 전달할 **SUNAPI 명령어 직접 입력**

> ★★★ **EdgeTrack 프로젝트에 직접 적용 가능한 인사이트** ★★★
>
> EdgeTrack PTZ는 "객체를 추적하는 PTZ 카메라"인데, 여기에 **"객체가 카메라 시야를 벗어나면 다른 카메라로 핸드오버"** 시나리오를 추가하면 한화 도메인 이해도 어필 가능.
>
> 팀 R&R에서는 카메라가 2대뿐이라 어려울 수 있지만, **"핸드오버 명령을 SUNAPI Custom Query 형태로 송신하는 인터페이스"** 만 구현해도 충분히 어필됨. (실제 핸드오버 동작은 더미 카메라로 대체)
>
> 면접 답변: *"한화 SUNAPI의 핸드오버 동작 방식을 분석하여, EdgeTrack에서도 객체 추적 중 시야 이탈 시 다른 카메라로 명령을 송신할 수 있는 Custom Query 인터페이스를 구현했습니다."*

### 7.3 NVR 사용자 코딩 (이벤트 액션)

NVR의 이벤트 액션 화면에서 **사용자가 SUNAPI 명령어를 직접 작성**해 채널별로 실행 가능.

**UI**:
```
[이벤트 액션]
  ▼ 사용자 코딩

  /stw-cgi/  [image.cgi?msubmenu=focus&action=control&Mode=SimpleFocus]
                                                          ← SUNAPI 명령어 입력

  채널 선택: CH 1 / CH 2 / CH 3 / CH 4    [TEST]
```

**예시 명령**: `image.cgi?msubmenu=focus&action=control&Mode=SimpleFocus`
→ "이벤트 발생 시 채널 1번 카메라에 자동 포커스 실행"

> EdgeTrack에서 **PIR 모션 센서가 이벤트 발생 → SUNAPI 명령으로 카메라 동작 트리거**하는 시나리오로 활용 가능. (STM32-2 담당의 PIR 모듈과 연동)

---

## 8. ★ 주의 사항: WiseAI는 SUNAPI와 경로가 다르다 ★

영상 분석 기능은 **수행 위치에 따라** 두 가지로 분류되며, **API 경로가 완전히 다르다**.

### 8.1 두 가지 영상 분석 분류

| 구분 | 네트워크 카메라 **내장** 분석 | **WiseAI** (오픈플랫폼 애플리케이션) |
|---|---|---|
| 위치 | 카메라 펌웨어 안 | 오픈 플랫폼 별도 앱 |
| API 경로 | `/stw-cgi/eventsources.cgi` | `/opensdk/WiseAI/...` |
| HTTP 메서드 | **GET only** | **GET / PUT / DELETE** |
| 문서 | SUNAPI 문서 | WiseAI 별도 API 문서 |

### 8.2 객체감지 명령어 비교 예시

**네트워크 카메라 내장 객체감지** (SUNAPI):
```
GET http://<Device IP>/stw-cgi/eventsources.cgi?msubmenu=objectdetection&action=<value>[&<parameter>=<value>]
```
→ HTTP **GET method only**

**WiseAI 객체감지** (OpenSDK):
```
GET/PUT/DELETE http://<Device IP>/opensdk/WiseAI/configuration/objectdetection
```
→ HTTP **GET / PUT / DELETE method use**

### 8.3 EdgeTrack 본인 기획안 보완 사항

본인 기획안의 SUNAPI 엔드포인트 목록에 `eventsources.cgi?msubmenu=videoanalysis` 가 스트레치로 들어가 있었는데, 캡쳐 자료를 보고 **명확히 정정**:

- **본인 SUNAPI 서버**에서는 `eventsources.cgi?msubmenu=objectdetection` 으로 객체감지 결과 반환 (GET only)
- 향후 확장 시 `/opensdk/WiseAI/configuration/objectdetection` 도 함께 모방하면 **"한화의 두 가지 영상 분석 체계를 모두 이해하고 있다"** 어필 가능

> 면접에서 받을 만한 질문: *"WiseAI와 일반 SUNAPI 분석 기능의 차이가 뭐죠?"*
> 답변: *"분석 로직이 카메라 펌웨어 내장이냐, 오픈 플랫폼 별도 앱이냐의 차이입니다. 그래서 API 경로도 `/stw-cgi/`와 `/opensdk/`로 나뉘고, 메서드도 내장은 GET only인 반면 WiseAI는 PUT/DELETE까지 지원해 동적 설정 변경이 가능합니다."*

---

## 9. EdgeTrack 본인 파트 (RTSP/SUNAPI HTTP 서버) 업데이트 액션 아이템

이번 자료를 반영해 본인 개인 기획안에 반영할 항목:

1. **응답 형식 두 가지 지원**: `?format=text` / `?format=json` 쿼리 파라미터로 분기 → 한화 실제 동작과 일치
2. **응답 필드명 정확히 차용**: `Model`, `SerialNumber`, `FirmwareVersion`, `BuildDate`, `CGIVersion`, `ONVIFVersion`, `DeviceType=NWC`
3. **URL 경로 정확화**: 본인 기획안의 `/stw-cgi/system.cgi?msubmenu=deviceinfo&action=view` 는 실제 한화 포맷과 정확히 일치 ✓
4. **HTTP 메서드 구분**:
   - 일반 SUNAPI: GET 위주
   - PTZ 제어: GET (control 액션)
   - 향후 WiseAI 영역 확장 시: GET/PUT/DELETE
5. **디바이스 매니저 호환 테스트를 포트폴리오 시연 영상에 반드시 포함** (한화 공식 툴이 우리 서버를 진짜 카메라로 인식)
6. **핸드오버 인터페이스 시나리오 추가 검토**: Custom Query 송신 인터페이스만이라도 구현 → "한화 도메인 이해" 가산점

---

## 10. 핵심 정리 (한 페이지 요약)

```
SUNAPI = Smart Unified Network API
       = 한화비전 장비 통합 제어 HTTP API
       = /stw-cgi/<카테고리>.cgi?msubmenu=<기능>&action=<동작>[&파라미터]

10개 CGI 카테고리:
  system / network / security / transer / video / image
  ptzcontrol / ptzconfig / eventsources / recording / opensdk

응답 형식: 텍스트(key=value) 또는 JSON 둘 다 지원

활용 사례:
  ① 모니터링 프로그램 개발
  ② SI 시스템 연동
  ③ 카메라 핸드오버 (Custom Query)
  ④ NVR 이벤트 액션 (사용자 코딩)

영상 분석은 두 갈래:
  - 카메라 내장 → /stw-cgi/eventsources.cgi (GET only)
  - WiseAI    → /opensdk/WiseAI/... (GET/PUT/DELETE)

테스트 툴: 웹브라우저 / 디바이스 매니저 / Postman / OnCode
```

---

---
---

# Part 2. RTSP (Real Time Streaming Protocol)

> 출처: 한화비전 공식 교육자료 (RTSP와 외부연동 섹션)
> 이번 정리의 핵심: **한화 RTSP URL 포맷은 일반 RTSP와 다르다** + **RTSP는 영상 전송 전용, 제어는 SUNAPI 영역**

---

## 11. RTSP 개요

### 11.1 정의

**RTSP = Real Time Streaming Protocol**

- **RFC 2326** 표준 프로토콜
- 비디오 및 오디오 사용에 대한 표준
- **실시간 영상**, **저장 영상 검색 및 다운로드** 등의 방법이 정의됨

### 11.2 ★★★ RTSP의 결정적 한계 (= EdgeTrack 설계의 핵심 근거)

> **RTSP는 카메라 설정/제어가 불가능하다.**

```
[카메라] ──RTSP 연동──> [저장장치]         [저장장치] ──SUNAPI 연동──> [카메라]
   ↑   영상전송 가능   │                       ↑   영상전송 가능       │
   │                   │                        │                       │
   └─── 제어 명령 ─────┘                       └──── 제어 명령 ─────────┘
        ❌ 전달 불가                                 ✅ 전달 가능
```

→ **RTSP는 "단방향 영상 파이프"**, 카메라를 움직이거나 설정을 바꾸려면 **반드시 SUNAPI(또는 ONVIF)가 필요**.

> ★ **EdgeTrack 본인 기획안의 RTSP/SUNAPI 분리 설계는 한화 공식 아키텍처와 정확히 일치** ★
>
> 본인 개인 기획안에서:
> - RTSP 서버 → 영상 전송 (포트 8554)
> - SUNAPI HTTP 서버 → PTZ 제어 명령 (포트 80) → UART → STM32
>
> 이 구조가 우연이 아니라 **한화의 실제 제품 아키텍처를 정확히 모방**하는 것임을 면접에서 어필 가능.
>
> 면접 답변 (예상): *"한화 공식 자료에 따르면 RTSP는 영상 전송 전용이고 카메라 제어는 RTSP로 불가능합니다. 그래서 EdgeTrack도 RTSP는 영상 송출 전용으로 두고, PTZ 제어는 SUNAPI HTTP 서버에서 받아 STM32로 포워딩하는 식으로 책임을 분리했습니다."*

---

## 12. RTSP 메서드 시퀀스 (Client ↔ Server)

한화 공식 자료의 시퀀스 다이어그램:

```
Client                                              Server
  │                                                   │
  ├────────────── OPTIONS ─────────────────────────►│
  │◄──────── 지원하는 Method 종류 응답 ──────────────┤
  │                                                   │
  ├────────────── DESCRIBE ────────────────────────►│
  │◄──── RTSP URL (Media stream) 응답 (= SDP) ──────┤
  │                                                   │
  ├────────────── SETUP ───────────────────────────►│
  ├────────────── PLAY ────────────────────────────►│
  │                                                   │
  │◄═══════════ Media Stream ═══════════════════════│
  │◄═══════════ Media Stream ═══════════════════════│
  │◄═══════════ Media Stream ═══════════════════════│
  │                                                   │
  ├────────────── TEARDOWN ────────────────────────►│
  │                                                   │
```

> ★ **EdgeTrack 개인 기획안의 RTSP 구현 범위와 정확히 일치** ★
>
> 본인 기획안의 Level 2 구현 범위(OPTIONS / DESCRIBE / SETUP / PLAY / TEARDOWN 필수)가 한화 공식 시퀀스와 그대로 매칭됨. **PAUSE/GET_PARAMETER/SET_PARAMETER**는 선택으로 둔 것도 합리적인 판단 (아래 13.7 참조).

---

## 13. 한화 비전 카메라 RTSP URL 포맷 ★

> **여기가 가장 중요한 부분.** 일반적인 RTSP URL과 한화 RTSP URL은 다르다.

### 13.1 핵심 변수

| 변수 | 의미 | 비고 |
|---|---|---|
| `Profile<no>` | 비디오 프로파일 번호 | **1~10** (해상도/코덱 등 미리 설정된 프로파일) |
| `<chid>` | 채널 번호 | **0부터 시작** (n번 채널 재생 시 n-1 입력) |
| `<profile name>` | 프로파일 이름 (별칭) | 숫자 대신 이름으로도 호출 가능 |

→ 예: 2번 채널을 재생하려면 `<chid>=1`

### 13.2 라이브 모니터링 URL

**일반 카메라**:
```
rtsp://<device IP>/profile<no>/media.smp
rtsp://<device IP>/<profile name>/media.smp
```

**멀티채널 카메라** (멀티센서 카메라 또는 가상채널 지원 단일 센서):
```
rtsp://<device IP>/<chid>/profile<no>/media.smp
rtsp://<device IP>/<chid>/<profile name>/media.smp
```

### 13.3 저장영상(SD카드) 재생 URL

**시간 포맷**: `YYYYMMDDHHMMSS` (연/월/일/시/분/초, 카메라 설정 시간 기준)
- 예) `20250101180415` = 2025년 1월 1일 18시 4분 15초

**일반 카메라**:
```
rtsp://<device IP>/recording/play.smp
rtsp://<device IP>/recording/<Start Time>/play.smp
rtsp://<device IP>/recording/<Start Time>-<End Time>/play.smp
```

**멀티채널 카메라**:
```
rtsp://<device IP>/<chid>/recording/play.smp
rtsp://<device IP>/<chid>/recording/<Start Time>/play.smp
rtsp://<device IP>/<chid>/recording/<Start Time>-<End Time>/play.smp
```

### 13.4 한화 RTSP URL의 4가지 특이점 ★

1. **`.smp` 확장자** — 일반 RTSP 서버는 `/live`, `/stream` 같은 단순 경로를 쓰지만 한화는 `media.smp`, `play.smp` 같은 `.smp` 확장자를 사용. 한화 고유 컨벤션.
2. **`profile<no>` 경로** — 클라이언트가 URL에서 직접 프로파일(해상도/코덱)을 선택. 일반 RTSP는 SDP에서 협상하는데 한화는 URL에서 선택 가능.
3. **`/recording/` 서브경로** — 저장 영상 재생은 별도 경로. 라이브와 명확히 분리.
4. **`<chid>` 채널 인덱스** — 멀티센서/가상채널 지원. 0-indexed (사람이 보는 1번 채널 = `<chid>=0`).

---

## 14. 한화 비전 저장장치(NVR) RTSP URL 포맷

> 카메라가 아닌 NVR(녹화기) RTSP는 **포트 558** 사용 (카메라는 기본 554).

### 14.1 라이브 모니터링

```
rtsp://<device IP>:558/LiveChannel/<chid>/media.smp
rtsp://<device IP>:558/LiveChannel/<chid>/media.smp/session=<sid>
```

- `session=<sid>`: 세션 ID

### 14.2 녹화 영상 재생

**시간 포맷** (카메라와 다름!):
- `YYYYDDTHHMMSS` — 현지시간 (예: `20250101T180415`)
- `YYYYDDTHHMMSSZ` — UTC시간 (`Z` 접미사)

**파라미터**:
- `session=<sid>`: 세션 ID
- `overlap=<id>`: 덮어쓰기 트랙 정보 (NVR이 같은 시간대 영상을 여러 번 녹화한 경우 트랙 구분)
- `substream`: 듀얼레코딩 지원 시 서브 스트림 재생

**URL**:
```
rtsp://<device IP>:558/PlaybackChannel/<chid>/media.smp
rtsp://<device IP>:558/PlaybackChannel/<chid>/media.smp/overlap=<id>&session=<sid>
rtsp://<device IP>:558/PlaybackChannel/<chid>/media.smp/start=<Start Time>&end=<End Time>&overlap=<id>&session=<sid>
```

### 14.3 카메라 RTSP vs NVR RTSP 비교

| 항목 | 카메라 | NVR(저장장치) |
|---|---|---|
| RTSP 포트 | 554 (기본) | **558** |
| 라이브 경로 | `/profile<no>/media.smp` | `/LiveChannel/<chid>/media.smp` |
| 재생 경로 | `/recording/...` | `/PlaybackChannel/<chid>/...` |
| 시간 포맷 | `YYYYMMDDHHMMSS` (현지) | `YYYYDDTHHMMSS` / `...Z` (UTC) |
| 세션 ID | 없음 | `session=<sid>` |
| 덮어쓰기 트랙 | 없음 | `overlap=<id>` |

→ NVR이 더 복잡한 건 다중 카메라 + 다중 트랙 + 시간 기반 검색을 지원해야 하기 때문.

---

## 15. 한화 SSM(VMS) RTSP URL 포맷

SSM (Smart Security Manager) = 한화의 VMS. **MediaGateway**라는 중계 서버를 통해 카메라에 접근.

### 15.1 라이브 모니터링

```
rtsp://<MediaGateway IP>:<RTSP Port>/<Camera Uuid>/<Profile ID>/media.smp
rtsp://<MediaGateway IP>:<RTSP Port>/shortcut_<shortcut>/<Profile ID>/media.smp
```

### 15.2 녹화 영상 재생

```
rtsp://<MediaGateway IP>:<RTSP Port>/<Camera Uuid>/<Profile ID>/playback?overlap=<overlap>
rtsp://<MediaGateway IP>:<RTSP Port>/shortcut_<shortcut>/<Profile ID>/playback?overlap=<overlap>
```

### 15.3 특이점

- **MediaGateway 경유**: 카메라 IP가 아닌 게이트웨이 IP로 접속 → 다수 카메라를 게이트웨이가 프록시
- **Camera Uuid** 또는 **shortcut**: 카메라 식별자 (IP 대신 논리 ID 사용)
- **Profile ID**: 카메라가 아닌 SSM에서 정의된 프로파일

→ SSM은 본인 EdgeTrack 프로젝트 스코프 밖이지만, **"한화 카메라 → NVR → SSM 3단 RTSP 구조를 이해하고 있다"**고 면접에서 언급할 수 있는 무기.

---

## 16. RTSP 클라이언트 검증 — VLC 플레이어

### 16.1 기본 검증 방법

1. VLC 실행 → **미디어 > 네트워크 스트림 열기** (Ctrl+N)
2. 네트워크 주소 입력:
   ```
   rtsp://<카메라IP>:<RTSP포트>/profile<No.>/media.smp
   ```
   예) `rtsp://192.168.1.101:554/profile4/media.smp`
3. **접속 정보 입력 필요**: 관리자 계정의 ID와 비밀번호

### 16.2 VLC 외 플레이어 호환성

- VLC 외에도 네트워크 주소 재생을 지원하는 플레이어에서 사용 가능
- 단, **플레이어 사양에 따라 일부 동작 미지원** 가능
- **VLC 등 상용 플레이어는 특정 Method 제어 기능 미구현** (예: `SET_PARAMETER`, `GET_PARAMETER`)

> ★ **EdgeTrack 본인 기획안 검증** ★
>
> 본인 기획안의 RTSP Level 2 구현 범위에서:
> - `GET_PARAMETER` / `SET_PARAMETER` → "선택/스트레치"로 분류한 것 → **정확한 판단**
> - 이유: VLC가 어차피 이 메서드를 안 쓰기 때문에, MVP에서는 OPTIONS/DESCRIBE/SETUP/PLAY/TEARDOWN 5개만 확실히 구현하면 VLC 호환 100% 달성 가능
>
> 면접 답변: *"VLC 같은 상용 클라이언트는 SET_PARAMETER 같은 메서드를 쓰지 않기 때문에, 한화처럼 모든 메서드를 다 구현하기보다 핵심 5개만 견고하게 구현하고 나머지는 스트레치 목표로 분류했습니다."*

---

## 17. ★★★ EdgeTrack 본인 파트 — 한화 호환 모드 옵션 추가 ★★★

지금까지 본인 기획안의 RTSP URL은:
```
rtsp://<RPi IP>:8554/live
```

이건 일반적인 GStreamer/Live555 패턴. **한화 호환을 위해 듀얼 모드를 권장**:

### 17.1 듀얼 URL 라우팅 (권장)

```cpp
// RTSP 서버에서 두 패턴 모두 받아들이기

// 패턴 A: 표준/심플 (기존)
"rtsp://<ip>:8554/live"

// 패턴 B: 한화 호환 (신규 추가) ★
"rtsp://<ip>:8554/profile1/media.smp"
"rtsp://<ip>:8554/profile2/media.smp"     // 다른 해상도/코덱 프로파일
"rtsp://<ip>:8554/<profile_name>/media.smp"
```

### 17.2 추가 구현 시 어필 포인트

1. **Profile 개념 도입** — 1개 카메라에서 여러 해상도/코덱 동시 제공
   - `profile1` = 1080p H.264 (메인)
   - `profile2` = 720p H.264 (서브)
   - `profile3` = D1 MJPEG (저대역)
   - → SDP를 프로파일별로 다르게 생성하는 로직 추가
2. **`.smp` 확장자 라우팅** — `media.smp`로 끝나는 경로를 라이브 스트림으로 라우팅
3. **포트 번호 의도적 선택**
   - MVP: 8554 (개발 편의)
   - 최종 데모: 554 (한화 표준 포트와 일치, root 권한 필요)
4. **인증 추가** — 한화 카메라처럼 RTSP에 ID/비밀번호 인증 필요 (HTTP Basic 또는 RTSP Digest)

> 면접 답변: *"한화의 RTSP URL 컨벤션이 `/profile<no>/media.smp`인 것을 분석해서, 제 RTSP 서버도 이 포맷을 라우팅에 추가했습니다. 한 카메라에서 여러 인코딩 프로파일을 동시에 제공하는 한화의 멀티프로파일 구조를 모방하기 위함입니다."*

### 17.3 SDP `a=control` 필드와 한화 URL의 관계

본인 기획안의 SDP 예시:
```
m=video 0 RTP/AVP 96
a=rtpmap:96 H264/90000
a=control:track1
```

한화 호환 시:
```
m=video 0 RTP/AVP 96
a=rtpmap:96 H264/90000
a=control:trackID=1               ; 또는 track1
; 컨테이너 control은 *
a=control:*
```

VLC가 `SETUP` 요청 시 `rtsp://.../profile1/media.smp/trackID=1` 형태로 보내므로, **URL 파서가 trackID 서브경로까지 파싱**해야 함. 본인 기획안의 RtspSession에 `track_id` 필드 추가 권장.

---

## 18. EdgeTrack 8주 일정 보강 — 한화 호환 모드 끼워넣기

기존 본인 개인 기획안 8주 일정에 **한화 호환 추가 작업** 끼워넣기:

| Week | 기존 작업 | **한화 호환 추가 작업 (선택)** |
|---|---|---|
| 1 | TCP 소켓, OPTIONS/DESCRIBE 파서 | URL 라우터에 `/profile<no>/media.smp` 패턴 추가 |
| 2 | SDP 생성, SETUP 핸들러, 세션 ID | SDP `a=control` 필드 한화 스타일로 |
| 3 | ⭐ VLC 첫 재생 | VLC에서 `rtsp://.../profile1/media.smp` 형식으로도 재생 확인 |
| 4 | FU-A 단편화, SPS/PPS, 타임스탬프 | (그대로) |
| 5 | 멀티클라이언트(epoll), TEARDOWN | profile1/profile2 두 프로파일 동시 제공 |
| 6 | SUNAPI HTTP 서버 5~6개 엔드포인트 | 한화 응답 필드명 정확히 차용 (Part 1 참조) |
| 7 | UART + STM32 연동 | 디바이스 매니저로 카메라 인식 확인 |
| 8 | 데모 영상, 포트폴리오 정리 | **한화 RTSP/SUNAPI 호환 시연 영상에 추가** |

→ 추가 작업량은 크지 않은데 어필 효과는 매우 큼.

---

## 19. RTSP + SUNAPI 통합 정리 — EdgeTrack 한 페이지 요약

```
┌─────────────────────────────────────────────────────────────────┐
│ 외부 클라이언트 (VLC, Wisenet WAVE, 디바이스 매니저)             │
└────────┬───────────────────────────┬────────────────────────────┘
         │ RTSP/RTP                  │ HTTP (SUNAPI)
         │ (영상 전송 전용)          │ (제어 명령 전용)
         │ rtsp://<ip>:8554/         │ http://<ip>/stw-cgi/
         │   profile1/media.smp      │   <category>.cgi?msubmenu=...
         ▼                           ▼
┌──────────────────────────────────────────────────────────────────┐
│              라즈베리파이 — 내 C++ 바이너리                       │
│  ┌──────────────┐                  ┌─────────────────┐           │
│  │ RTSP 서버    │                  │ SUNAPI HTTP 서버 │           │
│  │ - OPTIONS    │                  │ - system.cgi    │           │
│  │ - DESCRIBE   │                  │ - network.cgi   │           │
│  │ - SETUP      │                  │ - image.cgi     │           │
│  │ - PLAY       │                  │ - ptzcontrol.cgi│           │
│  │ - TEARDOWN   │                  │ - eventsources  │           │
│  │              │                  └────────┬────────┘           │
│  │ RTP 패킷화   │                           │ PTZ 명령           │
│  │ FU-A 분할    │                           ▼                    │
│  │ → UDP 5004   │                  ┌────────────────┐            │
│  └──────────────┘                  │ UART Bridge    │            │
│                                    └────────┬───────┘            │
└─────────────────────────────────────────────┼────────────────────┘
                                              │ UART 115200
                                              ▼
                                       ┌─────────────┐
                                       │   STM32     │
                                       │   PTZ 모터  │
                                       └─────────────┘
```

**한 줄 요약**:
> 영상은 RTSP(`/profile<no>/media.smp` 한화 포맷)로 송출하고, 제어는 SUNAPI(`/stw-cgi/<cgi>.cgi?msubmenu=...&action=...`)로 받아 UART로 STM32에 포워딩한다. 이는 한화비전의 실제 IP 카메라 아키텍처와 동일한 책임 분리이다.

---

---
---

# Part 3. ONVIF (Open Network Video Interface Forum)

> 출처: 한화비전 공식 교육자료 (ONVIF와 외부연동 섹션)
> 이번 정리의 핵심: **ONVIF의 영상/제어 분리 구조가 본인 EdgeTrack 설계 사상과 완전히 일치** + **ONVIF Profile S 구현 시 필요한 정확한 URI 경로**

---

## 20. ONVIF 개요

### 20.1 정의

**ONVIF = Open Network Video Interface Forum** (https://www.onvif.org)

- **네트워크 비디오 장치 간 통신의 표준화** 단체
- **제조업체와 무관한 네트워크 비디오 제품 간 상호 운용성** 확보
- 기존 웹 서비스에서 널리 통용되는 표준 프로토콜을 이용하여 정의됨

### 20.2 ONVIF가 다루는 두 영역 ★★★

**ONVIF는 단일 프로토콜이 아니라, 두 프로토콜의 결합이다.**

| 영역 | 사용 프로토콜 | 데이터 형식 |
|---|---|---|
| **영상/음성 전송** | **RTSP** | RTP 페이로드 (H.264/H.265 등) |
| **카메라 설정/제어/이벤트** | **WSDL & SOAP** | **XML** |

→ **ONVIF = RTSP(영상) + SOAP(제어)** 의 표준화된 묶음.

> ★ **EdgeTrack 본인 파트 설계 사상과 100% 일치** ★
>
> 본인 기획안의 핵심 분리:
> - 영상 송출 → RTSP 서버 (직접 구현)
> - 카메라 제어 → SUNAPI HTTP 서버 (직접 구현)
>
> 이 구조는 **ONVIF의 표준 분리(RTSP + SOAP)와 동일한 사상**. 본인이 SUNAPI를 HTTP+JSON으로, ONVIF가 SOAP+XML로 한다는 차이만 있을 뿐 **"영상 따로, 제어 따로"** 라는 산업 표준 패턴을 그대로 따라간 것.
>
> 면접 답변: *"ONVIF가 영상은 RTSP로, 제어는 SOAP+XML로 분리한 것과 동일한 사상으로, EdgeTrack에서도 RTSP는 영상 전용, SUNAPI HTTP는 제어 전용으로 분리했습니다. 즉 본 프로젝트는 ONVIF의 SOAP을 한화 자체 표준인 SUNAPI(JSON/CGI)로 대체한 형태로 볼 수 있습니다."*

---

## 21. ONVIF Application Type (Device vs Client)

| Type | 역할 | 예시 |
|---|---|---|
| **Device** | CCTV 카메라처럼 **영상 등을 제공하는 주체** (서버) | 한화 IP 카메라, NVR |
| **Client** | SSM처럼 **모니터링 SW로 Device로부터 영상 등을 제공받는 주체** | Wisenet WAVE, SSM, VLC |

> **EdgeTrack 본인 프로젝트는 Device 측을 만든다.** ONVIF Device 인증 절차를 그대로 모방하지는 않더라도, "Device 역할"을 정확히 이해하고 있다는 점을 어필 가능.

---

## 22. ONVIF 버전 표기 — SUNAPI와의 통합 ★

ONVIF 버전은 한화 카메라의 SUNAPI 응답에 **`ONVIFVersion` 필드로 포함**된다.

```
GET /stw-cgi/system.cgi?msubmenu=deviceinfo&action=view

→ 응답에 포함:
ONVIFVersion=24.06
              │  └─ 월 (month)
              └──── 연 (year)
```

- 예) `ONVIFVersion=24.06` = 2024년 6월판
- 예) `ONVIFVersion=22.12` = 2022년 12월판 (ODM 데모 화면)
- 예) `ONVIFVersion=20.12` = 2020년 12월판 (Part 1의 XND-C9083RV)

> ★ **EdgeTrack 본인 파트 적용** ★
>
> 본인의 SUNAPI HTTP 서버 응답에 `ONVIFVersion` 필드를 추가하고, 실제 ONVIF Profile S 구현 시점의 버전(예: `25.06`)을 박아넣으면 **"SUNAPI 응답과 ONVIF 서비스가 동일 펌웨어 안에서 일관되게 제공된다"**는 완성도를 어필 가능.

---

## 23. ONVIF 프로파일 체계

ONVIF는 제공 기능의 집합을 **Profile**이라는 카테고리로 구분.

한화 카메라 웹 UI의 "응용 인터페이스" 메뉴에는 다음이 함께 노출됨:
- **ONVIF Profile S/G/T/M**
- **SUNAPI (HTTP API)**
- **Hanwha Vision Open Platform**

→ 즉 한화 카메라는 이 세 가지 API를 동시에 제공.

### 23.1 주요 ONVIF Profile 비교

| Profile | 기능 범위 | EdgeTrack 구현 시 우선순위 |
|---|---|---|
| **S** | 기본 설정(사용자/IP/이미지), 영상/음성 전송, 이벤트 전송, **PTZ 제어**, Digital I/O | **★★ 필수 (본인 기획안 그대로)** |
| **G** | 비디오 스토리지, 녹화, 검색 | 스킵 (SD/NVR 기능) |
| **T** | H.264 고급 활용, **H.265**, 이미징 설정, **영상분석 이벤트 추가** | 선택 (AI 추적 연동 시) |
| **M** | 분석 애플리케이션을 위한 **메타데이터 및 이벤트** | 선택 (객체 추적 메타데이터 송출 시) |
| Q/A/C 등 | 기타 (Quick Install / Access Control / Cloud) | 스킵 |

### 23.2 EdgeTrack에 어울리는 프로파일 조합

본인 메인 기획안의 ONVIF 구현 범위는 **Profile S + Profile T 일부**.

→ 한화 공식 분류와 완전히 일치:
- Profile S: PTZ 제어 + 영상/음성 전송 = EdgeTrack의 주력 기능
- Profile T: 영상분석 이벤트 = AI 객체 탐지 결과를 ONVIF Event로 송출

> 면접 답변: *"EdgeTrack은 PTZ 추적 카메라이므로 Profile S를 메인으로, AI 객체 탐지 이벤트를 VMS에 전달하기 위해 Profile T의 영상분석 이벤트를 일부 추가했습니다. 한화 신형 카메라들이 Profile S/G/M/T를 모두 지원하는 추세인 만큼, S와 T 조합은 실제 상용 카메라의 최소 사양에 가깝습니다."*

---

## 24. 한화비전의 ONVIF 호환 현황 (시장 분석용)

ONVIF 공식 홈페이지(`onvif.org`) → **Conformant Products** 페이지에서 검색 가능.
- Manufacturer = `Hanwha Vision` 선택 시 **264개 제품**이 ONVIF 인증

### 한화 카메라 ONVIF 인증 예시 (2025년 5월 기준)

| 제품명 | Application Type | 지원 프로파일 | 펌웨어 버전 |
|---|---|---|---|
| SPA-P100 | Device | S | 3.4.5.28 |
| SPA-W100 | Device | S | 3.4.5.28 |
| TNM-C2712TDR | Device | **M, S, T** | 2.21.00_20250508_R397 |
| TNO-C3050T | Device | **G, M, S, T** (전 프로파일!) | 2.20.61_20250513_R131 |
| TNM-C2722TDR | Device | M, S, T | 2.21.00_20250508_R397 |
| SPA-C110 | Device | S | 3.4.5.28 |
| **XND-A9084RV** | Device | **G, M, S, T** | 25.01.00_20250328_R392 |

→ **인사이트**: 한화 제품은 보급형(SPA 시리즈)은 Profile S만, 상위 모델은 G/M/S/T를 모두 지원. **EdgeTrack의 "Profile S + T 일부" 조합은 최소 사양 ~ 중급 사양 수준에 해당**.

### ONVIF FeatureList.xml 구조 (호환 확인 자료)

ONVIF 사이트에서 특정 장비 선택 시 **FeatureList**, **DoC**, **Interface Guide** XML 다운로드 가능.

XND-A9084RV의 FeatureList.xml 발췌:
```xml
<Datasheet xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
           xmlns:xsd="http://www.w3.org/2001/XMLSchema">
  <DeviceInformation>
    <Manufacturer>Hanwha Vision</Manufacturer>
    <Model>XND-A9084RV</Model>
    <SerialNumber>ZVVF70GY10001YN</SerialNumber>
    <FirmwareVersion>25.01.00_20250328_R392</FirmwareVersion>
    <HardwareID>XND-A9084RV</HardwareID>
    <RelayOutputs>1</RelayOutputs>
    <DigitalInputs>1</DigitalInputs>
  </DeviceInformation>
  <DeviceDetails>...</DeviceDetails>
  <TestInformation>
    <TestDate>2025-04-08T09:14:45.0928686+09:00</TestDate>
    <ProductName>XND-A9084RV</ProductName>
    <ProductType><string>Fixed Camera</string></ProductType>
    <ToolVersion>24.12 rev. 270</ToolVersion>
  </TestInformation>
  <ConformanceInformation>
    <SupportedProfile>
      <Profile>M</Profile>
      <Profile>T</Profile>
      <Profile>S</Profile>
      <Profile>G</Profile>
    </SupportedProfile>
    <SupportedAddon/>
  </ConformanceInformation>
</Datasheet>
```

→ **포트폴리오 어필 포인트**: EdgeTrack도 자체 FeatureList.xml을 작성해 `<SupportedProfile><Profile>S</Profile></SupportedProfile>` 형태로 ONVIF 호환 정보를 명시 가능. 실제 공식 인증을 받을 수는 없지만, **"ONVIF 표준 양식을 따라 자가 선언했다"**는 점이 임팩트.

---

## 25. ONVIF Device Manager (ODM) — 검증 도구 ★

### 25.1 개요

- **ODM = ONVIF Device Manager**: ONVIF 프로토콜을 준수하는 장치를 테스트하고 관리하는 **프리웨어 소프트웨어**
- 한화 자체의 디바이스 매니저와는 별개의 ONVIF 표준 검증 툴

### 25.2 ODM 사용 흐름

1. **관리자 계정 로그인**: Name / Password 입력
2. **Device list → Add 버튼**: 장치 추가
3. **URI 입력**: `http://<카메라 IP>/onvif/device_service`
   - 예: `http://192.168.1.107/onvif/device_service`
4. **자동 디스커버리**: 같은 네트워크 카메라 자동 검색 가능

### 25.3 ODM이 제공하는 관리 메뉴

ODM이 카메라에서 받아와 표시하는 정보:
- **Identification**: Name, Location, Manufacturer, Model, Hardware, Firmware, Device ID, IP/MAC, **ONVIF version**, URI
- **Time settings / Maintenance / Network settings / User management**
- **Certificates / System log / Relays / Web page / Events**
- **NVT**: Network Video Transmitter (영상 송출 채널)
- **Live video / Video streaming / Imaging settings / Analytics / Rules / Metadata / Profiles**

### 25.4 ODM Imaging Settings 화면 (이미지 설정 상세) ★★★

ODM의 이미지 설정 화면에 표시되는 **RTSP URL**이 매우 중요:

```
rtsp://192.168.1.107:554/0/onvif/profile1/media.smp
                      │ │  │       │
                      │ │  │       └─ 한화 프로파일 이름
                      │ │  └────── ONVIF 표준 경로
                      │ └────────── chid (멀티채널 인덱스, 0 = 1번 채널)
                      └──────────── 표준 RTSP 포트
```

→ **한화 카메라가 ONVIF용으로 노출하는 RTSP URL 포맷**:
```
rtsp://<device IP>:554/<chid>/onvif/profile<no>/media.smp
```

### 25.5 ODM에서 조정 가능한 이미지 파라미터

- Brightness (50.00), Color saturation (50.00), Contrast (50.00), Sharpness (12.00)
- White balance mode (auto), White balance Cb (403.00), White balance Cr (604.00)
- Backlight compensation Mode (off) / Level (50.00)
- Wide dynamic range mode (off) / level (0.00)
- Infrared cutoff filter settings (on)

→ **이 모든 설정이 ONVIF SOAP `SetImagingSettings`로 제어 가능**. ISP 튜닝(B 담당)과 연결되는 영역.

---

## 26. ★★★ ONVIF 서비스 URI 표준 경로 (본인 기획안 보완 정보) ★★★

> 본인 개인 기획안에 빠져있던 **정확한 ONVIF SOAP 엔드포인트 URI**.

### 26.1 ONVIF 표준 Device Service URI

```
http://<device IP>/onvif/device_service
```

→ ODM이 첫 접속 시 이 URI로 SOAP 요청을 보냄. **본인 ONVIF 서비스 구현의 첫 진입점.**

### 26.2 일반적인 ONVIF 서비스 경로 패턴

(한화 공식 자료에 명시되진 않았지만 ONVIF 표준)

| 서비스 | 표준 경로 |
|---|---|
| Device Service | `/onvif/device_service` |
| Media Service | `/onvif/media_service` |
| PTZ Service | `/onvif/ptz_service` |
| Event Service | `/onvif/event_service` |
| Imaging Service | `/onvif/imaging_service` |
| Analytics Service | `/onvif/analytics_service` |

→ 일부 벤더는 모든 서비스를 단일 엔드포인트로 통합하고 SOAP Action으로 분기하기도 함. **본인 구현은 단순화를 위해 단일 엔드포인트 + SOAPAction 라우팅 권장**.

### 26.3 ONVIF RTSP 미디어 URL 표준 경로

ODM이 `GetStreamUri` 요청을 보내면 카메라는 RTSP URL을 응답한다. 한화의 응답 형식:

```
rtsp://<device IP>:554/<chid>/onvif/profile<no>/media.smp
```

→ **본인 기획안의 RTSP 라우터에 이 패턴도 추가하면 ONVIF 호환성 확보**.

### 26.4 WS-Discovery (네트워크 자동 발견)

- 멀티캐스트 주소: **239.255.255.250**, 포트 **3702** (UDP)
- ONVIF Probe → ProbeMatch 응답으로 카메라 자동 발견
- 본인 기획안의 "WS-Discovery 응답 구현" 항목과 정확히 일치 ✓

---

## 27. ★★★ EdgeTrack 본인 파트 — ONVIF Profile S 통합 액션 아이템 ★★★

### 27.1 본인 기획안의 ONVIF 범위 (정리)

본인 메인 기획안에서:
- ONVIF Discovery (WS-Discovery)
- ONVIF Device Service: GetDeviceInformation, GetCapabilities, GetSystemDateAndTime
- ONVIF Media Service: GetProfiles, GetStreamUri, GetVideoEncoderConfiguration
- ONVIF PTZ Service: ContinuousMove, Stop, GetStatus

→ **Profile S의 핵심 6개 함수로 한정** — 합리적 스코핑.

### 27.2 자료 반영 후 보완 사항

1. **ONVIF 서비스 URI를 정확한 표준 경로로**:
   - Device Service: `http://<ip>/onvif/device_service`
   - Media Service: `http://<ip>/onvif/media_service`
   - PTZ Service: `http://<ip>/onvif/ptz_service`

2. **RTSP URL 응답을 한화 ONVIF 포맷으로**:
   - `GetStreamUri` 응답에 `rtsp://<ip>:554/0/onvif/profile1/media.smp` 반환
   - RTSP 서버 라우터도 이 패턴 받아들이도록 추가

3. **`GetDeviceInformation` 응답 필드를 한화 ONVIF + SUNAPI 일관성 있게**:
   - Manufacturer = "VEDA Academy" (또는 "Hanwha Vision Clone")
   - Model, FirmwareVersion, SerialNumber, HardwareId
   - SUNAPI `deviceinfo` 응답과 동일한 값 사용 → "두 API가 같은 펌웨어를 보고 있다"

4. **`GetCapabilities` 응답에 명시할 서비스 목록**:
   ```xml
   <tt:Device>
     <tt:XAddr>http://<ip>/onvif/device_service</tt:XAddr>
   </tt:Device>
   <tt:Media>
     <tt:XAddr>http://<ip>/onvif/media_service</tt:XAddr>
   </tt:Media>
   <tt:PTZ>
     <tt:XAddr>http://<ip>/onvif/ptz_service</tt:XAddr>
   </tt:PTZ>
   ```

5. **자체 FeatureList.xml 생성** (포트폴리오용):
   - `<SupportedProfile><Profile>S</Profile></SupportedProfile>` 자가 선언
   - ONVIF 표준 양식 따라 작성 → GitHub README에 첨부

### 27.3 검증 시나리오 추가 (데모 영상에 포함)

본인 기획안의 데모 시나리오에 다음 단계 추가:

1. **ODM(ONVIF Device Manager)** 실행
2. Add device → `http://<RPi IP>/onvif/device_service` 입력
3. 자동으로 카메라 정보 인식 (Name, Manufacturer, Model, ONVIF version 표시)
4. **Live video** 메뉴에서 RTSP 영상 재생 (자체 RTSP 서버에서 송출)
5. **PTZ 컨트롤**로 화면에서 직접 카메라 회전 (→ ONVIF → UART → STM32 → 모터)
6. **Imaging settings** 슬라이더로 Brightness 조절 (→ B의 ISP 파이프라인까지 연동되면 보너스)

→ **이 시연 하나가 한화 Wisenet WAVE 시연보다 더 강한 임팩트**. 이유: ODM은 ONVIF 표준 검증 도구이므로 "표준 준수성" 자체를 증명함.

---

## 28. ★ EdgeTrack — RTSP + SUNAPI + ONVIF 3중 호환 통합 다이어그램 ★

```
┌────────────────────────────────────────────────────────────────────────┐
│  외부 클라이언트 (3가지 인터페이스 동시 지원)                            │
│  ┌─────────────┐  ┌──────────────────┐  ┌──────────────────────────┐  │
│  │   VLC       │  │ 한화 디바이스 매니저│  │ ONVIF Device Manager(ODM)│  │
│  │ (RTSP only) │  │   (SUNAPI HTTP)   │  │  (ONVIF SOAP + RTSP)     │  │
│  └──────┬──────┘  └─────────┬────────┘  └────────────┬─────────────┘  │
└─────────┼───────────────────┼──────────────────────────┼────────────────┘
          │ RTSP/RTP          │ HTTP (SUNAPI)            │ SOAP + RTSP
          ▼                   ▼                          ▼
          ┌──────────────────────────────────────────────────────┐
          │       라즈베리파이 — 내 C++ 바이너리 (단일 프로세스)  │
          │  ┌──────────────────┐   ┌──────────────────────────┐ │
          │  │   RTSP 서버      │   │  HTTP 서버               │ │
          │  │  포트 554/8554   │   │  포트 80                 │ │
          │  │                  │   │  ┌────────────────────┐  │ │
          │  │  URL 라우터:     │   │  │  /stw-cgi/ → SUNAPI│  │ │
          │  │  • /live         │   │  │  ─ system.cgi      │  │ │
          │  │  • /profile<n>/  │   │  │  ─ ptzcontrol.cgi  │  │ │
          │  │    media.smp     │   │  │  ─ image.cgi       │  │ │
          │  │  • /<chid>/onvif/│   │  │  ─ eventsources.cgi│  │ │
          │  │    profile<n>/   │   │  └────────────────────┘  │ │
          │  │    media.smp     │   │  ┌────────────────────┐  │ │
          │  │                  │   │  │ /onvif/* → ONVIF   │  │ │
          │  │  RTP 패킷화      │   │  │ ─ device_service   │  │ │
          │  │  (FU-A, RFC 6184)│   │  │ ─ media_service    │  │ │
          │  └─────────┬────────┘   │  │ ─ ptz_service      │  │ │
          │            │            │  │ (SOAP/XML)         │  │ │
          │            │            │  └─────────┬──────────┘  │ │
          │            │            └────────────┼─────────────┘ │
          │            │                         │               │
          │            │            ┌────────────┴────────────┐  │
          │            │            │   UART Bridge          │  │
          │            │            │   (단일 채널, 두 API 공유)│  │
          │            │            └────────────┬────────────┘  │
          └────────────┼─────────────────────────┼───────────────┘
                       │ RTP/UDP                 │ UART 115200
                       ▼                         ▼
              외부 VMS로 영상 송출       ┌──────────────┐
                                          │   STM32      │
                                          │   PTZ 모터   │
                                          └──────────────┘
```

### 핵심 설계 원칙

1. **단일 RTSP 서버, 다중 URL 패턴**: 일반 / 한화 / ONVIF 세 가지 URL 모두 라우팅
2. **단일 HTTP 서버, 경로 기반 분기**: `/stw-cgi/` → SUNAPI, `/onvif/` → ONVIF SOAP
3. **UART는 하나, 두 API가 공유**: SUNAPI PTZ 명령이든 ONVIF PTZ 명령이든 결국 동일한 UART 패킷으로 STM32에 전달
4. **응답 일관성**: 같은 정보(Model, Firmware 등)는 SUNAPI 응답과 ONVIF 응답에서 동일한 값 사용

> 면접 답변 (예상 질문: "왜 이렇게 세 가지를 다 구현했나요?")
>
> *"실제 한화 카메라 제품은 SUNAPI(자사 표준), ONVIF(국제 표준), 그리고 RTSP(영상 전송)를 동시에 노출합니다. EdgeTrack도 같은 카메라 1대가 세 종류의 클라이언트(자체 PC 툴, 표준 ONVIF 툴, 일반 VLC)를 모두 지원하도록, 한 프로세스 안에서 세 인터페이스를 동시에 서비스했습니다. 핵심 비즈니스 로직(PTZ 모터 제어, 영상 인코딩)은 한 곳에 있고, 그 위에 세 가지 API 어댑터를 얹은 구조입니다."*

---

## 29. ONVIF 정리 요약 (한 페이지)

```
ONVIF = Open Network Video Interface Forum
      = 네트워크 비디오 장치 표준 (제조사 무관 상호 운용성)
      = RTSP(영상) + SOAP/XML(제어, 이벤트)

Application Type:
  ─ Device (카메라/NVR 측, 영상 제공자)
  ─ Client (VMS 측, 영상 수신자)

주요 Profile:
  ─ S : 기본 + PTZ + 영상/음성 + 이벤트 + DI/O    ★ EdgeTrack 주력
  ─ G : 비디오 스토리지 + 녹화 + 검색
  ─ T : H.264/H.265 + 영상분석 이벤트            ★ EdgeTrack 일부
  ─ M : 메타데이터 + 분석 이벤트

표준 서비스 URI:
  ─ http://<ip>/onvif/device_service
  ─ http://<ip>/onvif/media_service
  ─ http://<ip>/onvif/ptz_service

한화 ONVIF RTSP URL:
  rtsp://<ip>:554/<chid>/onvif/profile<no>/media.smp

WS-Discovery:
  239.255.255.250:3702 (UDP 멀티캐스트)

버전 표기:
  YY.MM 형식 (예: 24.06 = 2024년 6월판)
  SUNAPI 응답의 ONVIFVersion 필드에 명시됨

검증 도구:
  ─ ONVIF Device Manager (ODM) — 표준 검증 프리웨어
  ─ ONVIF Conformant Products 페이지 (onvif.org)
  ─ FeatureList.xml / DoC / Interface Guide 다운로드

한화 ONVIF 인증 현황: 264개 제품
  ─ 보급형(SPA): Profile S만
  ─ 신형 상위(TNO, XND): G/M/S/T 전체 지원
```

---

*이후 추가 자료(WiseAI / 제품군 / 영상분석 / 카메라 라인업 등)는 본 파일 하단에 Part 4, Part 5 형태로 누적 정리 예정.*


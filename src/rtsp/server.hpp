// src/rtsp/server.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// 최상위 RTSP 서버 클래스 — epoll 기반 멀티클라이언트 이벤트 루프.
// ─────────────────────────────────────────────────────────────────────────────
//
// [구조 (Week 5)]
//
//   epoll_wait ──┬── 리스닝 fd 이벤트 → accept → Session 생성, epoll 등록
//                └── 클라이언트 fd 이벤트 → read → Session::feed
//                                          (끊김/TEARDOWN → 등록 해제 + 정리)
//
//   RTP 송출은 이 루프 밖이다: PLAY를 받은 Session이 media::Hub에 자기
//   Streamer를 구독시키면, Hub의 펌프 스레드가 카메라/파일 NAL을 모든
//   구독자에게 분배한다. 그래서 누가 PLAYING 중이어도 accept와 다른
//   클라이언트의 RTSP 요청 처리가 멈추지 않는다.
//
// [왜 epoll인가 (select/poll 대비)]
//   - 관심 fd 집합을 커널에 등록해 두고 "변한 것만" 받아온다 → O(이벤트 수)
//   - select는 호출마다 fd_set 전체 복사 + 1024 제한, poll은 매번 전체 스캔
//   - 카메라 서버처럼 "연결은 적고 수명은 긴" 워크로드에도 관용구가 깔끔하다

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace veda::rtsp {

class Server {
public:
    // port        : RTSP 리스닝 포트 (보통 554, 우리는 8554 사용)
    // source_path : 영상 소스. "camera"면 libcamera-vid, 아니면 .h264 파일 경로.
    Server(uint16_t port, std::string source_path);
    ~Server();

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    // 메인 이벤트 루프. SIGINT(Ctrl+C)가 올 때까지 블로킹.
    // 반환값은 프로세스 종료 코드 (main에 그대로 넘김).
    int run();

private:
    // 클라이언트 한 명 = TCP 연결 + RTSP 세션 묶음. server.cpp에서 정의.
    struct Client;

    uint16_t    port_;
    std::string source_path_;
};

}  // namespace veda::rtsp

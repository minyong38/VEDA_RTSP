// src/rtsp/server.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// 최상위 RTSP 서버 클래스.
// ─────────────────────────────────────────────────────────────────────────────
//
// [현재 구조]
//   Server::run() 안에서 단순한 accept 루프를 돈다:
//     while (살아있음):
//       client_fd = listener.accept_one()   ← 한 명 받기 (블로킹)
//       handle_client(client_fd)             ← 그 한 명만 처리
//   클라이언트가 끊겨야 다음 사람을 받는다 = 직렬 처리. (싱글 클라이언트)
//
// [향후 (Week 5)]
//   epoll을 도입해 여러 클라이언트를 동시에 처리하도록 바꿀 예정.
//   그땐 Session 클래스가 살아나서 클라이언트별 상태머신을 들고 있게 됨.

#pragma once

#include <cstdint>
#include <string>

namespace veda::rtsp {

class Server {
public:
    // port        : RTSP 리스닝 포트 (보통 554, 우리는 8554 사용)
    // source_path : 영상 소스 경로. 지금은 카메라가 우선이라 사용 안 함.
    //               향후 파일 재생 모드를 추가할 때 쓸 자리.
    Server(uint16_t port, std::string source_path);
    ~Server();

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    // 메인 이벤트 루프. SIGINT(Ctrl+C)가 올 때까지 블로킹.
    // 반환값은 프로세스 종료 코드 (main에 그대로 넘김).
    int run();

private:
    // 한 클라이언트 연결의 라이프사이클 전체를 처리.
    // - RTSP 요청 읽기/파싱/응답
    // - PLAY 받으면 RTP 스트리밍 루프 진입 (지금은 같은 함수 안에서 처리)
    void handle_client(int client_fd);

    uint16_t    port_;
    std::string source_path_;
};

}  // namespace veda::rtsp

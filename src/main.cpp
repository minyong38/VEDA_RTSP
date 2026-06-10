// src/main.cpp
//
// ─────────────────────────────────────────────────────────────────────────────
// VEDA_RTSP 엔트리 포인트.
// ─────────────────────────────────────────────────────────────────────────────
//
// [실행 예시]
//   ./veda_rtsp                                  # 기본: 포트 8554, 카메라 소스
//   ./veda_rtsp --port 8554 --source camera      # 라즈베리파이 카메라 (libcamera-vid)
//   ./veda_rtsp --port 8554 --source test.h264   # H.264 파일 루프 재생 (개발 PC용)
//
// [테스트용 H.264 파일 만들기]
//   ffmpeg -f lavfi -i testsrc=duration=60:size=640x480:rate=30
//          -c:v libx264 -preset ultrafast -tune zerolatency
//          -x264-params keyint=30 -f h264 test.h264
//
// [클라이언트 접속]
//   ffplay  -rtsp_transport udp rtsp://<ip>:8554/stream
//   vlc     rtsp://<ip>:8554/stream
//
// [구성 요소]
//   main()이 하는 일은 거의 없다 — 인자만 파싱하고 Server에 위임.
//   진짜 일은 src/rtsp/server.cpp의 epoll 루프 안에서 일어난다.

#include "rtsp/server.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// CLI 인자 묶음. 추후 옵션이 늘어도 이 구조체만 키우면 됨.
struct CliArgs {
    uint16_t    port   = 8554;       // RTSP 표준은 554지만
                                     // 권한 없이 띄울 수 있게 8554 기본값
    std::string source = "camera";   // "camera" 또는 .h264 파일 경로
};

// 손으로 짠 단순한 인자 파서.
// 형식: --key value 의 반복. 모르는 옵션은 그냥 무시.
// 진짜 프로젝트라면 cxxopts나 CLI11 같은 라이브러리를 쓰겠지만 학습용이라 직접.
CliArgs parse_args(int argc, char** argv) {
    CliArgs a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        if (k == "--port" && i + 1 < argc) {
            // atoi: 실패 시 0 반환. 검증 없이 그냥 캐스팅하므로 잘못된 입력에
            // 약하지만, 일단 동작에는 문제 없음.
            a.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (k == "--source" && i + 1 < argc) {
            a.source = argv[++i];
        }
    }
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    const CliArgs args = parse_args(argc, argv);

    // Server는 RAII로 모든 자원(리스닝 소켓 등)을 관리한다.
    // run()은 SIGINT가 올 때까지 블로킹하다 종료 코드를 돌려준다.
    veda::rtsp::Server server(args.port, args.source);
    std::printf("[veda_rtsp] listening on rtsp://0.0.0.0:%u/stream (source: %s)\n",
                args.port, args.source.c_str());

    return server.run();
}

// src/main.cpp
//
// ─────────────────────────────────────────────────────────────────────────────
// VEDA_RTSP 엔트리 포인트.
// ─────────────────────────────────────────────────────────────────────────────
//
// [실행 예시]
//   ./veda_rtsp                              # 기본: 포트 8554
//   ./veda_rtsp --port 8554
//   ./veda_rtsp --port 8554 --source file.h264   # (향후 파일 모드용)
//
// [클라이언트 접속]
//   ffplay  -rtsp_transport udp rtsp://<ip>:8554/stream
//   vlc     rtsp://<ip>:8554/stream
//
// [구성 요소]
//   main()이 하는 일은 거의 없다 — 인자만 파싱하고 Server에 위임.
//   진짜 일은 src/rtsp/server.cpp의 run() 안에서 일어난다.

#include "rtsp/server.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// CLI 인자 묶음. 추후 옵션이 늘어도 이 구조체만 키우면 됨.
struct CliArgs {
    uint16_t    port   = 8554;                          // RTSP 표준은 554지만
                                                        // 권한 없이 띄울 수 있게 8554 기본값
    std::string source = "tools/samples/test.h264";     // 향후 파일 재생 모드용
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
    std::printf("[veda_rtsp] listening on rtsp://0.0.0.0:%u/stream\n", args.port);

    return server.run();
}

#!/usr/bin/env bash
# tools/test_with_ffplay.sh
#
# Quick smoke test: start veda_rtsp in the background, point ffplay at it,
# capture exit codes. Run from the project root.
#
# Usage:
#   ./tools/test_with_ffplay.sh                 # camera source (Raspberry Pi)
#   SOURCE=file ./tools/test_with_ffplay.sh     # generated .h264 file (any PC)
#   CLIENTS=3 SOURCE=file ./tools/test_with_ffplay.sh   # multi-client test
#
# Requires: ffmpeg (ffplay), built binary at build/veda_rtsp

set -euo pipefail

PORT="${PORT:-8554}"
BIN="${BIN:-build/veda_rtsp}"
SOURCE="${SOURCE:-camera}"
CLIENTS="${CLIENTS:-1}"
SAMPLE="tools/samples/test.h264"

if [[ ! -x "$BIN" ]]; then
    echo "Error: $BIN not found. Build first:"
    echo "  cmake -B build && cmake --build build -j"
    exit 1
fi

# 파일 모드: 샘플이 없으면 ffmpeg로 60초짜리 테스트 패턴을 생성한다.
# keyint=30 → 1초마다 IDR이라 클라이언트가 어느 시점에 붙어도 1초 안에 화면이 뜬다.
if [[ "$SOURCE" == "file" ]]; then
    if [[ ! -f "$SAMPLE" ]]; then
        echo "[test] generating $SAMPLE..."
        mkdir -p tools/samples
        ffmpeg -hide_banner -loglevel error \
               -f lavfi -i testsrc=duration=60:size=640x480:rate=30 \
               -c:v libx264 -preset ultrafast -tune zerolatency \
               -x264-params keyint=30 -f h264 "$SAMPLE"
    fi
    SOURCE="$SAMPLE"
fi

echo "[test] starting veda_rtsp on port $PORT (source: $SOURCE)..."
"$BIN" --port "$PORT" --source "$SOURCE" &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null || true; wait 2>/dev/null || true' EXIT

sleep 1

echo "[test] launching $CLIENTS ffplay client(s) (close the window(s) to end)..."
PIDS=()
for ((i = 0; i < CLIENTS - 1; i++)); do
    ffplay -loglevel warning -rtsp_transport udp "rtsp://127.0.0.1:$PORT/stream" &
    PIDS+=($!)
done
# 마지막 한 개는 포그라운드 — 이 창을 닫으면 스크립트가 끝난다.
ffplay -loglevel info -rtsp_transport udp "rtsp://127.0.0.1:$PORT/stream"

for pid in "${PIDS[@]:-}"; do
    kill "$pid" 2>/dev/null || true
done

#!/usr/bin/env bash
# tools/test_with_ffplay.sh
#
# Quick smoke test: start veda_rtsp in the background, point ffplay at it,
# capture exit codes. Run from the project root.
#
# Requires: ffmpeg (ffplay), built binary at build/veda_rtsp

set -euo pipefail

PORT="${PORT:-8554}"
BIN="${BIN:-build/veda_rtsp}"

if [[ ! -x "$BIN" ]]; then
    echo "Error: $BIN not found. Build first:"
    echo "  cmake -B build && cmake --build build -j"
    exit 1
fi

echo "[test] starting veda_rtsp on port $PORT..."
"$BIN" --port "$PORT" &
SERVER_PID=$!
trap "kill $SERVER_PID 2>/dev/null || true" EXIT

sleep 1

echo "[test] launching ffplay (close the window to end)..."
ffplay -loglevel info -rtsp_transport udp "rtsp://127.0.0.1:$PORT/stream"

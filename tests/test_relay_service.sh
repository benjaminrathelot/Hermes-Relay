#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CLI="${CLI:-$ROOT/build/hermes-cli}"
WORK_ROOT="$ROOT/work/test-relay-service-$$"
PORT=$((9700 + ($$ % 200)))
PID=""

cleanup() {
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
        kill -TERM "$PID" 2>/dev/null || true
        wait "$PID" 2>/dev/null || true
    fi
    rm -rf "$WORK_ROOT"
}
trap cleanup EXIT INT TERM

"$CLI" relay-init \
    --root "$WORK_ROOT" \
    --listen "127.0.0.1:$PORT" \
    --heartbeat-interval 1 \
    --log-rotate-bytes 512 \
    --log-rotate-keep 2 >/dev/null

"$CLI" relay-run --root "$WORK_ROOT" >/dev/null 2>&1 &
PID=$!

i=0
while [ "$i" -lt 15 ]; do
    if [ -f "$WORK_ROOT/run/status.json" ]; then
        break
    fi
    i=$((i + 1))
    sleep 1
done

[ -f "$WORK_ROOT/run/status.json" ]
"$CLI" relay-status --root "$WORK_ROOT" >"$WORK_ROOT/status-running.json"
grep -q '"state":"running"' "$WORK_ROOT/status-running.json"
grep -q "\"listen_addr\":\"127.0.0.1:$PORT\"" "$WORK_ROOT/status-running.json"
grep -q 'service_started' "$WORK_ROOT/logs/relay.jsonl"

kill -HUP "$PID"
sleep 1

kill -TERM "$PID"
wait "$PID"
PID=""

"$CLI" relay-status --root "$WORK_ROOT" >"$WORK_ROOT/status-stopped.json"
grep -q '"state":"stopped"' "$WORK_ROOT/status-stopped.json"
grep -q 'service_stopped' "$WORK_ROOT/logs/relay.jsonl"
[ ! -f "$WORK_ROOT/run/relay.pid" ]

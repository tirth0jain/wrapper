#!/usr/bin/env bash
# Regression test: /status must answer while keep-alive connections are held.
#
#   bash lite/test-status-starvation.sh [lite-binary] [port]
#
# WHY this exists. httplib serves ONE keep-alive connection from ONE worker
# thread for the connection's whole lifetime, and every /m3u8 and /key handler
# also holds a worker for the whole (serialized) Apple call it makes. With the
# old 8-thread pool, one album's 4-track burst — 4 amdl processes, each with its
# own keep-alive connection — filled the pool, so /status was queued instead of
# answered. The seedbox supervisor reads a /status that does not answer within
# 3s as "the wrapper is dead", SIGTERMs it and takes every in-flight download
# with it (measured 2026-09-15: 8-13 restarts/hour at peak).
#
# Case A is the fix's regression test (default pool must survive the load).
# Case B is the test's own control: with LITE_POOL_SIZE=2 the probe MUST fail,
# so a green run can never mean "the harness stopped testing anything".
set -eu
cd "$(dirname "$0")/.."
REPO="$PWD"
BIN="${1:-$REPO/rootfs/system/bin/lite}"
PORT="${2:-8099}"
HOLD="${HOLD:-8}"          # keep-alive connections to hold during the probe
PROBE_S="${PROBE_S:-1.5}"  # a busy-but-healthy wrapper answers in milliseconds

[ -x "$BIN" ] || { echo "FAILED: $BIN is not executable"; exit 1; }

# Hardlinks need the tree on ONE filesystem, so build it inside the repo (and
# clean it up) rather than in /tmp, which is often a separate mount.
TMP="$(mktemp -d "$REPO/.pool-test.XXXXXX")"
INST_PID=""
cleanup() {
  [ -n "$INST_PID" ] && kill "$INST_PID" 2>/dev/null || true
  local p
  p=$(ss -ltnpH "sport = :$PORT" 2>/dev/null | grep -o 'pid=[0-9]*' | head -1 | cut -d= -f2 || true)
  [ -n "${p:-}" ] && kill "$p" 2>/dev/null || true
  sleep 1
  rm -rf "$TMP"
}
trap cleanup EXIT

cp -al rootfs "$TMP/rootfs"
rm -f "$TMP/rootfs/system/bin/lite"
cp "$BIN" "$TMP/rootfs/system/bin/lite"

cat > "$TMP/hold.py" <<'PY'
import socket, sys, time
port, n, hold = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3])
socks = []
for _ in range(n):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(b"GET /status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n")
    s.settimeout(5)
    try:
        s.recv(4096)
    except Exception:
        pass
    socks.append(s)
time.sleep(hold)
PY

start_instance() {  # $1 = extra env assignments (may be empty)
  ( cd "$TMP" && env $1 setsid nohup "$REPO/wrapper-lite-rootless" \
      --base-dir ./data --host 127.0.0.1 --port "$PORT" >> "$TMP/lite.log" 2>&1 & )
  local i
  for i in $(seq 1 40); do
    sleep 1
    if curl -s -m 2 -o /dev/null "http://127.0.0.1:$PORT/status"; then return 0; fi
  done
  echo "FAILED: instance did not come up on :$PORT"; tail -5 "$TMP/lite.log"; return 1
}

stop_instance() {
  local p
  p=$(ss -ltnpH "sport = :$PORT" 2>/dev/null | grep -o 'pid=[0-9]*' | head -1 | cut -d= -f2 || true)
  [ -n "${p:-}" ] && kill "$p" 2>/dev/null || true
  sleep 2
}

# probe_ok — one /status through the shell, within PROBE_S seconds (0 = ok).
probe_ms() {
  local t
  t=$(curl -s -m "$PROBE_S" -o /dev/null -w '%{time_total}' "http://127.0.0.1:$PORT/status" 2>/dev/null) || { echo "timeout"; return 0; }
  echo "$t"
}

check_case() {  # $1 = label, $2 = pool env, $3 = expect ("ok"|"starved")
  local label="$1" env="$2" expect="$3" i result starved=0 answered=0
  start_instance "$env"
  python3 "$TMP/hold.py" "$PORT" "$HOLD" 6 &
  local holder=$!
  sleep 2
  for i in 1 2 3; do
    result="$(probe_ms)"
    if [ "$result" = "timeout" ]; then
      echo "  $label probe$i: NO ANSWER within ${PROBE_S}s"
      starved=$((starved + 1))
    else
      echo "  $label probe$i: answered in ${result}s"
      answered=$((answered + 1))
    fi
  done
  wait "$holder" 2>/dev/null || true
  stop_instance
  # "ok" must be solid (every probe answered); "starved" only has to show that
  # the harness CAN see starvation (one timed-out probe is enough — httplib
  # occasionally frees a worker mid-run, and a control that demands total
  # starvation would flake).
  if [ "$expect" = "ok" ] && [ "$answered" != "3" ]; then
    echo "FAILED: $label — $starved/3 probes were starved at the default pool"
    return 1
  fi
  if [ "$expect" = "starved" ] && [ "$starved" = "0" ]; then
    echo "FAILED: control — no probe starved at pool=2, so this test cannot detect starvation"
    return 1
  fi
  echo "  $label: OK ($answered answered, $starved starved)"
  return 0
}

echo "→ binary: $BIN  (pool $HOLD keep-alive connections, ${PROBE_S}s probe budget)"
rc=0
check_case "default pool ($HOLD conns)" "" ok || rc=1
check_case "control pool=2 ($HOLD conns)" "LITE_POOL_SIZE=2" starved || rc=1

if [ "$rc" = "0" ]; then
  echo "✅ /status survives keep-alive load at the default pool, and the control still starves"
else
  echo "❌ status-starvation regression test failed"
fi
exit "$rc"

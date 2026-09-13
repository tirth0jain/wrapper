#!/usr/bin/env bash
# Host test for watchdog_decide(): extracts the REAL decision function out of
# lite/lite_main.cpp and drives every state combination.
#
#   bash lite/test-watchdog.sh
#
# The point: the watchdog used to exit whenever the OLDEST IN-FLIGHT handler
# passed 30s. The server serializes all /key and /m3u8 handlers on one mutex
# with an 8-thread pool, so one slow Apple call made the queued handlers look
# stuck and killed the whole process — taking every other rip's in-flight
# downloads with it (live 2026-09-13: an album lost 9 of 13 tracks). The cases
# below pin the fix: waiters never trip it, the holder does.
set -eu
cd "$(dirname "$0")/.."

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# The enum is a single line; the function's range ends at the first column-0
# brace, so the enum must NOT be ranged (a range would swallow the function).
sed -n '/^enum WatchdogAction/p' lite/lite_main.cpp > "$TMP/extracted.inc"
sed -n '/^static WatchdogAction watchdog_decide/,/^}/p' lite/lite_main.cpp >> "$TMP/extracted.inc"

if ! grep -q 'watchdog_decide' "$TMP/extracted.inc"; then
  echo "FAILED: could not extract watchdog_decide from lite/lite_main.cpp"; exit 1
fi

{
  sed -e '/EXTRACTED_SOURCE_HERE/{
    r '"$TMP/extracted.inc"'
    d
  }' lite/test_watchdog.cpp
} > "$TMP/test.cpp"

g++ -std=c++11 -O0 -Wall -Wextra -Wno-unused-parameter "$TMP/test.cpp" -o "$TMP/test"
"$TMP/test"

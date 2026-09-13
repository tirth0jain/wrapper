#!/usr/bin/env bash
# Host test for webplayback_reason(): extracts the REAL function text out of
# lite/apple_api.cpp (plus strfmt, which it uses for the http-status fallback)
# and compiles it against the cJSON that the wrapper itself builds with.
#
#   bash lite/test-webplayback-reason.sh
#
# The point: the message this function returns is what the addon classifies a
# seedbox-side failure from, so it must keep Apple's dialog wording intact
# ("remove the explicit content restriction for Apple Music" is the phrase
# internal/applemusic/ripper.go keys on) and never contain a newline.
set -eu
cd "$(dirname "$0")/.."

CJSON_SRC="build/_deps/cjson-src"
[ -f "$CJSON_SRC/cJSON.c" ] || { echo "cJSON sources missing at $CJSON_SRC (configure the build first)"; exit 2; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

{
  # forward declaration + <cstdarg>: the shipped file defines strfmt far below
  # webplayback_reason, so the extracted pair needs the declaration replayed.
  echo '#include <cstdarg>'
  echo 'std::string strfmt(const char* fmt, ...);'
  sed -n '/^static std::string webplayback_reason/,/^}/p' lite/apple_api.cpp
  sed -n '/^std::string strfmt(const char\* fmt, \.\.\.)/,/^}/p' lite/apple_api.cpp
} > "$TMP/extracted.inc"

if ! grep -q 'webplayback_reason' "$TMP/extracted.inc"; then
  echo "FAILED: could not extract webplayback_reason from lite/apple_api.cpp"; exit 1
fi
if ! grep -q 'strfmt' "$TMP/extracted.inc"; then
  echo "FAILED: could not extract strfmt from lite/apple_api.cpp"; exit 1
fi

# Splice the extraction into the test file's marker.
{
  sed -e '/EXTRACTED_SOURCE_HERE/{
    r '"$TMP/extracted.inc"'
    d
  }' lite/test_webplayback_reason.cpp
} > "$TMP/test.cpp"

g++ -std=c++11 -O0 -Wall -Wextra -Wno-unused-parameter \
  -I "$CJSON_SRC" \
  "$TMP/test.cpp" "$CJSON_SRC/cJSON.c" -lm -o "$TMP/test"

"$TMP/test"

#!/usr/bin/env bash
# Quick health check. Run before every commit:  package/tools/smoke.sh
# Clean build with warnings treated as errors, unit tests, module list, short run, screenshot.
set -e
cd "$(dirname "$0")/../.."
fail() { echo "SMOKE FAIL: $*"; exit 1; }

make clean >/dev/null
make -j"$(nproc)" CXXFLAGS="-std=c++23 -O2 -Wall -Wextra -Wno-unused-result -Wno-unused-parameter -Werror -I package -I package/modules" >/dev/null \
    || fail "build (warnings count as errors)"

make test >/dev/null 2>&1 || { make test 2>&1 | grep -E "FAIL|tests,"; fail "unit tests"; }

mods=$(./space_game_v2 --list-modules 2>/dev/null | wc -l)
[ "$mods" -ge 8 ] || fail "expected >= 8 modules, got $mods"

out=$(mktemp -d)
./space_game_v2 --frames=60 --paused --screenshot="$out/shot.bmp" --screenshot-frame=30 2>"$out/log" \
    || fail "game exited non-zero (see $out/log)"
[ -s "$out/shot.bmp" ] || fail "no screenshot written"
grep -qi "failed\|threw\|skipped" "$out/log" && { cat "$out/log"; fail "module problem in log"; }

echo "SMOKE OK: tests pass, $mods modules, 60 frames, screenshot $out/shot.bmp"

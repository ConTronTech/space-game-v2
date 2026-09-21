#!/usr/bin/env bash
# Copy the SDL2 runtime libraries the game needs into libs/ next to the binary, for machines that do not have them (no sudo needed).
# The binary is linked with -rpath $ORIGIN/libs, so it finds them without LD_LIBRARY_PATH.
#   package/tools/bundle_libs.sh [binary] [dest]       default: ./space_game_v2 -> ./libs
# Run it on a machine that HAS the libraries (the dev PC), then copy libs/ with the game (package/tools/laptop_bench.sh does this for the test laptop).
set -euo pipefail
cd "$(dirname "$0")/../.."
BIN=${1:-space_game_v2}; DEST=${2:-libs}
mkdir -p "$DEST"
# libraries of the SDL2 family and their codec dependencies that a bare desktop often lacks; core system libs (libc, libGL, libX11...) are NOT bundled
ldd "$BIN" | awk '/=>/ {print $3}' | grep -E 'libSDL2|libopus|libogg|libvorbis|libFLAC|libmpg123|libmodplug|libfluidsynth|libpng|libjpeg|libtiff|libwebp|libfreetype|libharfbuzz|libgraphite|libbrotli|libsndfile|libinstpatch|libxmp|libwavpack|libtimidity|libpulsecommon|libavif|libjxl|libLerc|libdeflate|libjbig|libzstd' | sort -u | while read -r lib; do
  [ -e "$lib" ] && cp -L -u "$lib" "$DEST/" && echo "bundled $(basename "$lib")"
done
echo "done: $(ls "$DEST" | wc -l) libraries in $DEST/ (the game finds them by itself: rpath \$ORIGIN/libs)"

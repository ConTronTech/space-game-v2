#!/usr/bin/env bash
# Install the runtime libraries the game needs on a Debian/Ubuntu/Mint machine (needs sudo). Without sudo use package/tools/bundle_libs.sh instead.
#   sudo package/tools/install_deps.sh            runtime only (to RUN a built game)
#   sudo package/tools/install_deps.sh --dev      + compilers and -dev packages (to BUILD the game)
set -euo pipefail
RUN="libsdl2-2.0-0 libsdl2-image-2.0-0 libsdl2-ttf-2.0-0 libsdl2-mixer-2.0-0 libgl1 libglu1-mesa"
DEV="build-essential g++ make git libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libsdl2-mixer-dev libgl-dev libglu1-mesa-dev"
if [ "$(id -u)" != 0 ]; then echo "needs root: run with sudo (or use package/tools/bundle_libs.sh, no sudo)"; exit 1; fi
PKGS="$RUN"; [ "${1:-}" = "--dev" ] && PKGS="$RUN $DEV"
apt-get update -y
apt-get install -y $PKGS
echo "installed: $PKGS"

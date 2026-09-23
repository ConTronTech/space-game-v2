#!/usr/bin/env bash
# Sync the built game to the test laptop and run the benchmark there (no sudo, nothing left running).
#   package/tools/laptop_bench.sh [extra game flags]      e.g.  --quality=low   or   --disable=ship/cockpit
# Env: LAPTOP_HOST (e.g. user@192.168.1.x, no default - set it for your own test machine), LAPTOP_KEY (default ~/.ssh/id_ed25519_spacegame_laptop),
#      LAPTOP_DIR (default Documents/Space-Game-V2/space-game-v2, mirrors the main rig's actual relative path), BENCH_SECONDS (default 15), SYNC=0 to skip the copy.
# The laptop lacks the SDL2_image/ttf/mixer runtime libs, so they are shipped in libs/ (LD_LIBRARY_PATH). It never copies
# config/game.json or settings.json, so the laptop runs the defaults (auto quality).
set -euo pipefail
cd "$(dirname "$0")/../.."
HOST=${LAPTOP_HOST:?set LAPTOP_HOST=user@host for your test machine}; KEY=${LAPTOP_KEY:-$HOME/.ssh/id_ed25519_spacegame_laptop}
DIR=${LAPTOP_DIR:-Documents/Space-Game-V2/space-game-v2}; SECS=${BENCH_SECONDS:-15}
SSHC="ssh -i $KEY -o IdentitiesOnly=yes -o BatchMode=yes"
if [ "${SYNC:-1}" = 1 ]; then
  L=/usr/lib/x86_64-linux-gnu
  $SSHC "$HOST" "mkdir -p ~/$DIR/libs ~/$DIR/config"
  rsync -a -e "$SSHC" space_game_v2 assets data "$HOST:~/$DIR/"
  rsync -a -e "$SSHC" config/input config/ui "$HOST:~/$DIR/config/"
  rsync -aL -e "$SSHC" $L/libopusfile.so.0 $L/libSDL2_image-2.0.so.0 $L/libSDL2_mixer-2.0.so.0 $L/libSDL2_ttf-2.0.so.0 "$HOST:~/$DIR/libs/"
fi
$SSHC "$HOST" "cd ~/$DIR && export DISPLAY=:0 LD_LIBRARY_PATH=libs SDL_AUDIODRIVER=dummy && timeout $((SECS+45)) ./space_game_v2 --benchmark=$SECS --no-vsync $* 2>&1 | grep -E 'BENCHMARK|frames:|average|1% low|worst|quality preset|renderer'"

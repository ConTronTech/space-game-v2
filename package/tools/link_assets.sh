#!/usr/bin/env bash
# Git worktrees don't contain the git-ignored assets/ folder (167 MB of models + skybox). Run this once in a
# fresh worktree to link it to the main checkout's copy. Safe to re-run; does nothing in the main checkout.
set -e
cd "$(dirname "$0")/../.."
main=$(git worktree list --porcelain | awk '/^worktree /{print $2; exit}')
here=$(pwd -P)
if [ "$main" = "$here" ]; then echo "main checkout: assets/ is the real folder"; exit 0; fi
[ -d "$main/assets" ] || { echo "no assets/ in the main checkout ($main)"; exit 1; }
ln -sfn "$main/assets" assets
echo "assets -> $main/assets"

#!/usr/bin/env bash
# Installs the repo's git hooks into the shared hooks folder (used by the main checkout AND every worktree).
set -e
cd "$(dirname "$0")/../.."
common=$(git rev-parse --git-common-dir)
mkdir -p "$common/hooks"
install -m 755 package/tools/hooks/pre-commit "$common/hooks/pre-commit"
echo "installed pre-commit hook -> $common/hooks/pre-commit"

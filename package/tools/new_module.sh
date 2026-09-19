#!/usr/bin/env bash
# Usage: tools/new_module.sh <category> <name>     e.g. tools/new_module.sh gameplay weapons
set -e
[ $# -eq 2 ] || { echo "usage: $0 <category> <name>"; exit 1; }
cd "$(dirname "$0")/.."   # package/
cat_="$1"; name="$2"
dir="modules/$cat_/$name"
[ -e "$dir" ] && { echo "$dir already exists"; exit 1; }
class=$(echo "$name" | sed -E 's/(^|_)([a-z])/\U\2/g')
mkdir -p "$dir"
sed -e "s/__CATEGORY__/$cat_/g" -e "s/__NAME__/$name/g" -e "s/__CLASS__/$class/g" \
    template/module_template/module.cpp > "$dir/$name.cpp"
echo "created $dir/$name.cpp  (class $class) - run 'make' and it is part of the game"

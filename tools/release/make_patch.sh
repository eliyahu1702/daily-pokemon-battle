#!/bin/sh
# Makes the release patch from a clean Pokémon Emerald (USA/Europe) and the built ROM, then
# checks that applying it gives back exactly the built ROM.
#
# Usage: tools/release/make_patch.sh EMERALD.gba OUT.bps [ROM]
# Needs Flips (https://github.com/Alcaro/Flips) on PATH, or FLIPS=/path/to/flips.
set -eu

EMERALD_SHA1=f3ae088181bf583e55daf962a92bb46f4f1d07b7
base=$1
out=$2
rom=${3:-pokeemerald.gba}
flips=${FLIPS:-flips}

if [ "$(sha1sum "$base" | cut -d' ' -f1)" != "$EMERALD_SHA1" ]; then
    echo "$base is not a clean Pokémon Emerald (USA/Europe)" >&2
    exit 1
fi
"$flips" --create --bps-delta "$base" "$rom" "$out" >/dev/null
check=$(mktemp)
"$flips" --apply "$out" "$base" "$check" >/dev/null
cmp "$check" "$rom"
rm "$check"
echo "$out: $(wc -c < "$out") bytes, checked against $rom"

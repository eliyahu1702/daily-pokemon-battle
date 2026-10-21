#!/bin/sh
# Builds the play-in-browser site into OUTDIR: the pages in site/, the patch, and a copy of
# EmulatorJS with its mGBA core (fetched from npm, so the site doesn't depend on another CDN).
#
# Usage: tools/release/build_site.sh PATCH.bps OUTDIR
set -eu

EMULATORJS_VERSION=4.2.3
patch=$1
out=$2
root=$(cd "$(dirname "$0")/../.." && pwd)

mkdir -p "$out"
cp -R "$root/site/." "$out/"
cp "$patch" "$out/daily-pokemon-battle.bps"

tmp=$(mktemp -d)
(cd "$tmp" && npm pack --silent "@emulatorjs/emulatorjs@$EMULATORJS_VERSION" "@emulatorjs/core-mgba@$EMULATORJS_VERSION" >/dev/null)
mkdir -p "$tmp/ejs" "$tmp/core"
tar -xzf "$tmp/emulatorjs-emulatorjs-$EMULATORJS_VERSION.tgz" -C "$tmp/ejs"
tar -xzf "$tmp/emulatorjs-core-mgba-$EMULATORJS_VERSION.tgz" -C "$tmp/core"
mkdir -p "$out/emulatorjs"
cp -R "$tmp/ejs/package/data" "$out/emulatorjs/"
cp "$tmp/ejs/package/LICENSE" "$out/emulatorjs/LICENSE"
mkdir -p "$out/emulatorjs/data/cores/reports"
cp "$tmp/core/package/"mgba-*.data "$out/emulatorjs/data/cores/"
cp "$tmp/core/package/reports/mgba.json" "$out/emulatorjs/data/cores/reports/"
rm -r "$tmp"

touch "$out/.nojekyll"
echo "site built in $out"

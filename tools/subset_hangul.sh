#!/usr/bin/env bash
# Rebuilds assets/fonts/Hangul-Subset.ttf, the Hangul fallback of the UI font (see
# docs/specs/korean.md): KS X 1001's 2,350 syllables, the compatibility jamo and every non-ASCII
# character in app/strings.c, cut out of Pretendard Regular. Run it again whenever the Korean
# strings gain a character.
#
# Usage: tools/subset_hangul.sh <Pretendard-Regular.ttf>
#   The TrueType file from Pretendard's release (public/static/alternative/Pretendard-Regular.ttf,
#   https://github.com/orioncactus/pretendard, SIL Open Font License).
#
# Needs Node.js. subset-font is fetched by npx for this run only; nothing is added to the repository.
set -euo pipefail

if [ $# -ne 1 ]; then
    sed -n '2,12p' "$0"
    exit 1
fi

here="$(cd "$(dirname "$0")" && pwd)"
export NV_SCRIPT="$here/subset_hangul.mjs" NV_INPUT="$1" NV_OUT="$here/../assets/fonts/Hangul-Subset.ttf" \
       NV_STRINGS="$here/../app/strings.c"
npx --yes -p subset-font@2.9.0 \
    -c 'NV_NODE_MODULES="$(dirname "$(printf "%s" "$PATH" | tr ":" "\n" | grep "/_npx/.*/node_modules/.bin" | head -1)")" node "$NV_SCRIPT"'

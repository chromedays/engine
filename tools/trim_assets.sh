#!/usr/bin/env bash
# Rebuilds assets/quaternius/ from the Quaternius packs (see docs/specs/animation.md).
#
# Usage: tools/trim_assets.sh <characters-dir> <animations-dir>
#   <characters-dir>  the unzipped "Universal Base Characters[Standard]" folder
#   <animations-dir>  the unzipped "Universal Animation Library[Standard]" folder
#
# Needs Node.js. gltf-transform and sharp are fetched by npx for this run only; nothing is added to
# the repository.
set -euo pipefail

if [ $# -ne 2 ]; then
    sed -n '2,10p' "$0"
    exit 1
fi

here="$(cd "$(dirname "$0")" && pwd)"
out="$here/../assets/quaternius"
mkdir -p "$out"

export NV_SCRIPT="$here/trim_assets.mjs" NV_CHARACTERS="$1" NV_ANIMATIONS="$2" NV_OUT="$out"
npx --yes \
    -p @gltf-transform/core@4.5.0 \
    -p @gltf-transform/functions@4.5.0 \
    -p @gltf-transform/cli@4.5.0 \
    -p sharp@0.35.4 \
    -c 'NV_NODE_MODULES="$(dirname "$(dirname "$(command -v gltf-transform)")")" node "$NV_SCRIPT"'

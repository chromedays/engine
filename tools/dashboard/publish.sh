#!/usr/bin/env bash
# Uploads a branch's auto-battler builds to R2 and registers them with the dashboard (docs/specs/build_dashboard.md).
# Run by CI (.github/workflows/build.yml, the publish jobs):
#   tools/dashboard/publish.sh <dir>     <dir> holds the install of release/ and debug/ (the engine-web artifact's autobattler/)
# Environment: BRANCH, SHA, REPO, DEFAULT_BRANCH, GH_TOKEN (for gh), CLOUDFLARE_API_TOKEN, CLOUDFLARE_ACCOUNT_ID,
# DASHBOARD_URL, DASHBOARD_TOKEN. R2_TARGET=--local writes to `wrangler dev`'s bucket instead, for a local test.
set -euo pipefail

WRANGLER=(npx --yes wrangler@4.150.0)
BUCKET=nv-builds
dir=$1
folder=${BRANCH//\//-}

for config in release debug; do
  test -f "$dir/$config/index.html"
  while IFS= read -r -d '' path; do
    file=${path#"$dir/$config/"}
    case "$file" in
      *.html) type="text/html; charset=utf-8" ;;
      *.js) type=text/javascript ;;
      *.wasm) type=application/wasm ;;
      *.json) type=application/json ;;
      *) type=application/octet-stream ;;
    esac
    "${WRANGLER[@]}" r2 object put "$BUCKET/builds/$folder/$config/$file" --file "$path" --content-type "$type" "${R2_TARGET:---remote}"
  done < <(find "$dir/$config" -type f -print0)
done

# Sizes: what a visitor downloads (the Debug build's DWARF only loads in developer tools), and everything the folder keeps.
bytes() { find "$@" -type f "${filter[@]}" -printf '%s\n' | awk '{ s += $1 } END { print s + 0 }'; }
filter=(! -name '*.debug.wasm')
release=$(bytes "$dir/release")
debug=$(bytes "$dir/debug")
filter=()
stored=$(bytes "$dir/release" "$dir/debug")

subject=$(gh api "repos/$REPO/commits/$SHA" --jq '.commit.message | split("\n")[0]')
date=$(gh api "repos/$REPO/commits/$SHA" --jq '.commit.committer.date')
if [ "$BRANCH" = "$DEFAULT_BRANCH" ]; then
  ahead=0
else
  # Commits master does not have; the dashboard counts a merged pull request's branch as in master.
  ahead=$(gh api "repos/$REPO/compare/$DEFAULT_BRANCH...$SHA" --jq '.ahead_by')
fi

jq -n --arg branch "$BRANCH" --arg sha "$SHA" --arg subject "$subject" --arg date "$date" \
  --argjson release "$release" --argjson debug "$debug" --argjson stored "$stored" --argjson ahead "$ahead" \
  '{ branch: $branch, sha: $sha, subject: $subject, date: $date, release: $release, debug: $debug, stored: $stored, ahead: $ahead }' |
  curl -fsS -X POST "$DASHBOARD_URL/api/builds" -H "Authorization: Bearer $DASHBOARD_TOKEN" -H "Content-Type: application/json" --data @-
echo "Published $BRANCH at $SHA: $DASHBOARD_URL/builds/$folder/release/"

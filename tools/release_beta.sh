#!/usr/bin/env bash
# Makes the next beta of a version: sets version.txt to vX.Y.Z-betaN, commits it and tags the commit.
# Pushing the tag makes CI publish the beta (.github/workflows/firmware-release.yml):
#
#   tools/release_beta.sh 2.1.0      # v2.1.0-beta1, then v2.1.0-beta2, ...
#   tools/release_beta.sh            # the next beta of the version in version.txt
#
# A beta comes before its release (v2.1.0-beta2 < v2.1.0): once v2.1.0 is released, the next betas
# are of a higher version. The release itself: tools/release.sh 2.1.0.
set -euo pipefail
cd "$(dirname "$0")/.."

# vX.Y.Z[-betaN] -> sortable key, a release after its betas
version_key() {
  [[ "$1" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)(-beta([0-9]+))?$ ]] || { echo 0; return; }
  printf '%05d%05d%05d%05d\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}" \
    "${BASH_REMATCH[5]:-99999}"
}

BASE="${1:-$(head -n1 version.txt | tr -d '\r')}"
BASE="${BASE#v}"
BASE="${BASE%%-*}"
[[ "$BASE" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "usage: $0 [X.Y.Z]" >&2; exit 1; }

git rev-parse -q --verify "refs/tags/v$BASE" >/dev/null \
  && { echo "v$BASE is released already: a beta goes before its release, take a higher version" >&2; exit 1; }
git diff --quiet HEAD -- version.txt || { echo "version.txt has uncommitted changes" >&2; exit 1; }

N=0
for t in $(git tag -l "v$BASE-beta*"); do
  [[ "$t" =~ -beta([0-9]+)$ ]] && (( BASH_REMATCH[1] > N )) && N=${BASH_REMATCH[1]}
done
TAG="v$BASE-beta$((N + 1))"

LATEST=$(git tag -l 'v*' | while read -r t; do echo "$(version_key "$t") $t"; done | sort | tail -n1 | cut -d' ' -f2)
if [ -n "$LATEST" ] && [[ "$(version_key "$TAG")" < "$(version_key "$LATEST")" ]]; then
  echo "$TAG is lower than $LATEST: clocks would not take it" >&2
  exit 1
fi

echo "$TAG" > version.txt
git add version.txt
git commit -q -m "Beta $TAG" -- version.txt
git tag -a "$TAG" -m "Beta $TAG"
echo "Tagged $TAG. Publish it (CI builds the beta and updates the \"beta\" channel):"
echo "  git push origin HEAD $TAG"

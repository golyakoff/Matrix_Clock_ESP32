#!/usr/bin/env bash
# Makes a release: sets version.txt to vX.Y.Z, commits it and tags the commit. Pushing the tag makes
# CI publish the release, which becomes the latest one (.github/workflows/firmware-release.yml):
#
#   tools/release.sh 2.1.0
#
# Betas of a version before it: tools/release_beta.sh 2.1.0.
set -euo pipefail
cd "$(dirname "$0")/.."

# vX.Y.Z[-betaN] -> sortable key, a release after its betas
version_key() {
  [[ "$1" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)(-beta([0-9]+))?$ ]] || { echo 0; return; }
  printf '%05d%05d%05d%05d\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}" \
    "${BASH_REMATCH[5]:-99999}"
}

VERSION="${1:?usage: $0 X.Y.Z}"
VERSION="${VERSION#v}"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "usage: $0 X.Y.Z" >&2; exit 1; }
TAG="v$VERSION"

git rev-parse -q --verify "refs/tags/$TAG" >/dev/null && { echo "$TAG exists already" >&2; exit 1; }
git diff --quiet HEAD -- version.txt || { echo "version.txt has uncommitted changes" >&2; exit 1; }

LATEST=$(git tag -l 'v*' | while read -r t; do echo "$(version_key "$t") $t"; done | sort | tail -n1 | cut -d' ' -f2)
if [ -n "$LATEST" ] && [[ "$(version_key "$TAG")" < "$(version_key "$LATEST")" ]]; then
  echo "$TAG is lower than $LATEST: clocks would not take it" >&2
  exit 1
fi

echo "$TAG" > version.txt
git add version.txt
git commit -q -m "Release $TAG" -- version.txt
git tag -a "$TAG" -m "Release $TAG"
echo "Tagged $TAG. Publish it (CI builds the release):"
echo "  git push origin HEAD $TAG"

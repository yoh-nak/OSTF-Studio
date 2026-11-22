#!/usr/bin/env bash
set -euo pipefail

output=$(mktemp)
trap 'rm -f "$output"' EXIT
export GITHUB_OUTPUT="$output" GITHUB_EVENT_NAME=workflow_dispatch GITHUB_REF=refs/heads/main
RELEASE_TYPE=none RELEASE_TAG='' bash packaging/release-version.sh
alpha=$(sed -n 's/^display=\(.*\)-dev$/\1/p' "$output")
test -n "$alpha"
: > "$output"
RELEASE_TYPE=prerelease RELEASE_TAG="v$alpha" bash packaging/release-version.sh
grep -Fxq "display=$alpha" "$output"

for tag in "v${alpha%-alpha.*}" "v$alpha.0" "v${alpha%-alpha.*}-beta.1"; do
  if RELEASE_TYPE=prerelease RELEASE_TAG="$tag" bash packaging/release-version.sh; then
    echo "Unexpectedly accepted $tag as an alpha" >&2
    exit 1
  fi
done
if GITHUB_EVENT_NAME=push RELEASE_TYPE=prerelease RELEASE_TAG="v$alpha" bash packaging/release-version.sh; then
  echo 'Unexpectedly accepted a release on push' >&2
  exit 1
fi

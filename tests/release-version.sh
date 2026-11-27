#!/usr/bin/env bash
set -euo pipefail

output=$(mktemp)
trap 'rm -f "$output"' EXIT
export GITHUB_OUTPUT="$output" GITHUB_EVENT_NAME=workflow_dispatch GITHUB_REF=refs/heads/main
RELEASE_TYPE=none RELEASE_TAG='' bash packaging/release-version.sh
alpha=$(sed -n 's/^display=\(.*\)-dev$/\1/p' "$output")
version=$(sed -n 's/^binary=//p' "$output")
[[ "$alpha" =~ ^[0-9]+\.[0-9]+\.[0-9]+-public-alpha\.[1-9][0-9]*$ ]]
test -s "docs/releases/v$alpha.md"
: > "$output"
RELEASE_TYPE=prerelease RELEASE_TAG="v$alpha" bash packaging/release-version.sh
grep -Fxq "display=$alpha" "$output"

for tag in "v$version" "v$alpha.0" "v$version-beta.1" "v$version-alpha.1" "v$version-alpha.2" "v$version-alpha.3"; do
  if RELEASE_TYPE=prerelease RELEASE_TAG="$tag" bash packaging/release-version.sh; then
    echo "Unexpectedly accepted $tag as an alpha" >&2
    exit 1
  fi
done
: > "$output"
RELEASE_TYPE=stable RELEASE_TAG="v$version" bash packaging/release-version.sh
grep -Fxq "display=$version" "$output"
if GITHUB_EVENT_NAME=push RELEASE_TYPE=prerelease RELEASE_TAG="v$alpha" bash packaging/release-version.sh; then
  echo 'Unexpectedly accepted a release on push' >&2
  exit 1
fi


# A draft can be resumed with the chosen number. Other products and the
# experimental alpha series do not change the public series.
inventory='[{"tag_name":"v1.0.0-public-alpha.39","draft":false},{"tag_name":"v1.0.0-public-alpha.40","draft":true},{"tag_name":"v2.0.0-public-alpha.9","draft":false},{"tag_name":"v1.0.0-alpha.100","draft":false}]'
python packaging/next_alpha.py v1.0.0-public-alpha.40 <<< "$inventory"
for wrong in 39 41; do
  if python packaging/next_alpha.py "v1.0.0-public-alpha.$wrong" <<< "$inventory"; then
    echo "Unexpectedly accepted alpha $wrong" >&2
    exit 1
  fi
done

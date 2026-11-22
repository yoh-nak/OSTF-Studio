#!/usr/bin/env bash
set -euo pipefail

version=$(sed -nE 's/^project\(OSTFStudio VERSION ([0-9]+\.[0-9]+\.[0-9]+) .*/\1/p' CMakeLists.txt)
alpha_number=$(sed -nE 's/^set\(OSTF_STUDIO_ALPHA_NUMBER ([1-9][0-9]*)\)[[:space:]]*$/\1/p' CMakeLists.txt)
test -n "$version"
test -n "$alpha_number"
alpha_version="$version-alpha.$alpha_number"
display_version="$alpha_version-dev"

if [[ "$RELEASE_TYPE" != none ]]; then
  if [[ "$GITHUB_EVENT_NAME" != workflow_dispatch || "$GITHUB_REF" != refs/heads/main ]]; then
    echo 'Releases require an explicit workflow dispatch on main.' >&2
    exit 1
  fi
  case "$RELEASE_TYPE" in
    stable) [[ "$RELEASE_TAG" == "v$version" ]] ;;
    prerelease) [[ "$RELEASE_TAG" == "v$alpha_version" ]] ;;
    *) false ;;
  esac || {
    echo "Expected v$alpha_version for an alpha or v$version for an explicitly authorized stable release." >&2
    exit 1
  }
  display_version="${RELEASE_TAG#v}"
fi

printf 'binary=%s\ndisplay=%s\n' "$version" "$display_version" >> "$GITHUB_OUTPUT"

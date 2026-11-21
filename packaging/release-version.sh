#!/usr/bin/env bash
set -euo pipefail

version=$(sed -nE 's/^project\(OSTFStudio VERSION ([0-9]+\.[0-9]+\.[0-9]+) .*/\1/p' CMakeLists.txt)
test -n "$version"
display_version="$version-preview"

if [[ "$RELEASE_TYPE" != none ]]; then
  if [[ "$GITHUB_EVENT_NAME" != workflow_dispatch || "$GITHUB_REF" != refs/heads/main ]]; then
    echo 'Releases require an explicit workflow dispatch on main.' >&2
    exit 1
  fi
  suffix_pattern='^[0-9A-Za-z]+([.-][0-9A-Za-z]+)*$'
  case "$RELEASE_TYPE" in
    stable) [[ "$RELEASE_TAG" == "v$version" ]] ;;
    prerelease) [[ "$RELEASE_TAG" == "v$version-"* && "${RELEASE_TAG#v$version-}" =~ $suffix_pattern ]] ;;
    *) false ;;
  esac || {
    echo 'The tag must match the CMake version. Use a suffix only for a pre-release.' >&2
    exit 1
  }
  display_version="${RELEASE_TAG#v}"
fi

printf 'binary=%s\ndisplay=%s\n' "$version" "$display_version" >> "$GITHUB_OUTPUT"

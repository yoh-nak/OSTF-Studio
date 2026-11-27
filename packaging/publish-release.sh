#!/usr/bin/env bash
set -euo pipefail

case "$RELEASE_TYPE" in
  stable) release_flags=(); release_title="OSTF Studio $RELEASE_TAG" ;;
  prerelease) release_flags=(--prerelease --latest=false); release_title="OSTF Studio Public Alpha ${RELEASE_TAG##*.}" ;;
  *) echo 'Select stable or prerelease to publish.' >&2; exit 1 ;;
esac
"$BASH" packaging/release-version.sh

prefix="dist/OSTF_Studio_${RELEASE_TAG#v}_"
assets=(
  "${prefix}Windows_x64_Setup.exe"
  "${prefix}Windows_x64_Portable.zip"
  "${prefix}macOS_arm64.dmg"
  "${prefix}macOS_x86_64.dmg"
  "${prefix}Corresponding_Source.tar.gz"
)
for asset in "${assets[@]}"; do
  test -s "$asset" || { echo "Missing release asset: $asset" >&2; exit 1; }
done
notes="docs/releases/$RELEASE_TAG.md"
test -s "$notes" || { echo "Missing release notes: $notes" >&2; exit 1; }

repo_api="repos/$GITHUB_REPOSITORY"
tag_ref="refs/tags/$RELEASE_TAG"
refs=$(gh api "$repo_api/git/matching-refs/tags/$RELEASE_TAG")
existing_sha=$(jq -r --arg ref "$tag_ref" '.[] | select(.ref == $ref) | .object.sha' <<< "$refs")
releases=$(gh api --paginate --slurp "$repo_api/releases")
if [[ "$RELEASE_TYPE" == prerelease ]]; then
  jq 'add // []' <<< "$releases" | python3 packaging/next_alpha.py "$RELEASE_TAG"
fi
entry=$(jq -c --arg tag "$RELEASE_TAG" '[.[][] | select(.tag_name == $tag)] | first // empty' <<< "$releases")
if [[ -n "$entry" ]]; then
  jq -e '.draft == true' <<< "$entry" >/dev/null || { echo "Already published: $RELEASE_TAG" >&2; exit 1; }
  [[ "$existing_sha" == "$GITHUB_SHA" ]] || { echo 'Draft tag does not match the built revision.' >&2; exit 1; }
fi
if [[ -z "$existing_sha" ]]; then
  gh api --method POST "$repo_api/git/refs" -f "ref=$tag_ref" -f "sha=$GITHUB_SHA" --silent
elif [[ "$existing_sha" != "$GITHUB_SHA" ]]; then
  echo "Tag already identifies another revision: $RELEASE_TAG" >&2
  exit 1
fi
if [[ -z "$entry" ]]; then
  gh release create "$RELEASE_TAG" --repo "$GITHUB_REPOSITORY" --verify-tag --draft \
    "${release_flags[@]}" --title "$release_title" --notes-file "$notes"
fi
id=$(gh api --paginate --slurp "$repo_api/releases" | jq -er --arg tag "$RELEASE_TAG" '[.[][] | select(.draft and .tag_name == $tag)] | first | .id')
list_assets() { gh api --paginate --slurp "$repo_api/releases/$id/assets" | jq 'add // []'; }
remote=$(list_assets)
for asset in "${assets[@]}"; do
  name=${asset##*/}
  if ! jq -e --arg name "$name" 'any(.[]; .name == $name)' <<< "$remote" >/dev/null; then
    gh release upload "$RELEASE_TAG" "$asset" --repo "$GITHUB_REPOSITORY"
  fi
done
remote=$(list_assets)
[[ $(jq 'length' <<< "$remote") == "${#assets[@]}" ]] || { echo 'Unexpected draft asset count' >&2; exit 1; }
for asset in "${assets[@]}"; do
  digest=$(sha256sum "$asset" | cut -d' ' -f1)
  size=$(stat -c %s "$asset")
  jq -e --arg name "${asset##*/}" --arg digest "sha256:$digest" --argjson size "$size" \
    'any(.[]; .name == $name and .size == $size and .digest == $digest and .state == "uploaded")' <<< "$remote" >/dev/null \
    || { echo "Draft asset mismatch: $asset" >&2; exit 1; }
done
gh release edit "$RELEASE_TAG" --repo "$GITHUB_REPOSITORY" --draft=false "${release_flags[@]}"

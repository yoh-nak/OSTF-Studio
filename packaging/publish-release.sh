#!/usr/bin/env bash
set -euo pipefail

case "$RELEASE_TYPE" in
  stable) release_flags=(); release_title="OSTF Studio $RELEASE_TAG" ;;
  prerelease) release_flags=(--prerelease --latest=false); release_title="OSTF Studio Public Alpha ${RELEASE_TAG##*.}" ;;
  *) echo 'Select stable or prerelease to publish.' >&2; exit 1 ;;
esac
"$BASH" packaging/release-version.sh

assets=(
  dist/OSTF_Studio_Windows_x64_Setup.exe
  dist/OSTF_Studio_Windows_x64_Portable.zip
  dist/OSTF_Studio_macOS_arm64.dmg
  dist/OSTF_Studio_Corresponding_Source.tar.gz
)
for asset in "${assets[@]}"; do
  if [[ ! -s "$asset" ]]; then
    echo "Missing release asset: $asset" >&2
    exit 1
  fi
done
notes="docs/releases/$RELEASE_TAG.md"
test -s "$notes" || { echo "Missing release notes: $notes" >&2; exit 1; }

repo_api="repos/$GITHUB_REPOSITORY"
tag_ref="refs/tags/$RELEASE_TAG"
tag_refs=$(gh api "$repo_api/git/matching-refs/tags/$RELEASE_TAG" --jq '.[].ref')
release_tags=$(gh api --paginate "$repo_api/releases" --jq '.[].tag_name')
if grep -Fxq "$tag_ref" <<< "$tag_refs" || grep -Fxq "$RELEASE_TAG" <<< "$release_tags"; then
  echo "Version already exists: $RELEASE_TAG" >&2
  exit 1
fi

# Creating a ref fails if another process creates the same tag concurrently.
gh api --method POST "$repo_api/git/refs" -f "ref=$tag_ref" -f "sha=$GITHUB_SHA" --silent
gh release create "$RELEASE_TAG" "${assets[@]}" --repo "$GITHUB_REPOSITORY" \
  --verify-tag "${release_flags[@]}" --title "$release_title" --generate-notes \
  --notes-file "$notes"

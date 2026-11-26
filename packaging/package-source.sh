#!/usr/bin/env bash
set -euo pipefail
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT
root="$staging/OSTF-Studio"
mkdir -p "$root/vendor/JUCE" "$root/build-tools/fluid-r3" dist
git archive HEAD | tar -xf - -C "$root"
git -C build/_deps/juce-src archive HEAD | tar -xf - -C "$root/vendor/JUCE"
link_src=build/_deps/ableton_link-src
mkdir -p "$root/vendor/link/modules/asio-standalone"
git -C "$link_src" archive HEAD | tar -xf - -C "$root/vendor/link"
git -C "$link_src/modules/asio-standalone" archive HEAD | tar -xf - -C "$root/vendor/link/modules/asio-standalone"
mkdir -p "$root/vendor/jsoncons"
cp -R build/_deps/jsoncons-src/{include,cmake,CMakeLists.txt,LICENSE} "$root/vendor/jsoncons/"
cp build-tools/fluid-r3/{FluidR3_GM.sf2,FluidR3-License.txt} "$root/build-tools/fluid-r3/"
test -s "$root/LICENSE"
test -s "$root/vendor/JUCE/CMakeLists.txt"
test -s "$root/vendor/JUCE/modules/juce_audio_devices/native/asio/LICENSE.txt"
test -s "$root/vendor/link/modules/asio-standalone/asio/include/asio.hpp"
test -s "$root/docs/images/ostf-studio-live-session.png"
# Exercise the offline dependency override before publishing this source tree.
cmake -S "$root" -B "$staging/check" -DFETCHCONTENT_SOURCE_DIR_JUCE="$root/vendor/JUCE" \
  -DFETCHCONTENT_SOURCE_DIR_JSONCONS="$root/vendor/jsoncons" -DFETCHCONTENT_SOURCE_DIR_ABLETON_LINK="$root/vendor/link" \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DCMAKE_BUILD_TYPE=Release -DOSTF_STUDIO_BUILD_GUI=ON
tar -czf dist/OSTF_Studio_Corresponding_Source.tar.gz -C "$staging" OSTF-Studio

# OSTF Studio licensing and corresponding source

The OSTF Studio audio editor and OSTF Harmonizer use the MIT-licensed Signalsmith Stretch 1.4.0 (commit `a670068d9aeb64913331d5cc29337b19a457a7df`) and Signalsmith Linear 0.6.4 (commit `de55e6a50ffcf6f8f43f649692d94691c7025151`). Their original headers and notices are under `third_party/signalsmith`. Distributions include Signalsmith-Stretch.txt and Signalsmith-Linear.txt. [Stretch source](https://github.com/Signalsmith-Audio/signalsmith-stretch/tree/a670068d9aeb64913331d5cc29337b19a457a7df) · [Linear source](https://github.com/Signalsmith-Audio/linear/tree/de55e6a50ffcf6f8f43f649692d94691c7025151)

The original OSTF Studio application and bundled VST3 plug-in source code is licensed under the GNU General Public License, version 3 only (SPDX identifier GPL-3.0-only). You may use, modify and redistribute this software under that license. The software is provided without warranty. The full license is included as LICENSE in the repository and GPL-3.0.txt in the distribution.

JUCE 9.0.5 is distributed under the GNU Affero General Public License, version 3. The GPLv3 and AGPLv3 terms apply to their respective parts under section 13 of each license. The JUCE upstream notice and software bill of materials identify its bundled dependencies. Steinberg's ASIO SDK components in the Windows build use their GPLv3 option. ASIO is a registered trademark of Steinberg Media Technologies GmbH. The VST3 SDK included with JUCE has its own MIT notice. The TinySoundFont engine and the FluidR3 GM sound bank retain their respective MIT notices. The official OSTF example retains its upstream CC0 designation. Third-party works and the OSTF specification retain their own terms.

Download OSTF_Studio_Corresponding_Source.tar.gz from the same GitHub Release as your installer. It contains the tagged Studio sources, the JUCE sources used in the build, the distributed FluidR3 GM bank and its notice, and the build and installer scripts. From the extracted source root, configure with a C++20 compiler and CMake 3.22 or newer:

```sh
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_JUCE="$PWD/vendor/JUCE" -DFETCHCONTENT_SOURCE_DIR_JSONCONS="$PWD/vendor/jsoncons" -DCMAKE_BUILD_TYPE=Release -DOSTF_STUDIO_BUILD_GUI=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Use Visual Studio 2022 x64 on Windows or Xcode Command Line Tools on macOS. The release workflow records platform-specific build options. See README.md and docs/RELEASING.md in the archive. [Release downloads and corresponding source](https://github.com/yoh-nak/OSTF-Studio/releases)

## Japanese factory voice

OSTF Formant Singer includes Japanese syllables synthesized, trimmed, normalized and looped from HTS Voice “Mei” by the MMDAgent Project Team, Copyright 2009–2013 Nagoya Institute of Technology, Department of Computer Science. The adapted voice samples are distributed under [Creative Commons Attribution 3.0](https://creativecommons.org/licenses/by/3.0/). See HTS-Voice-Mei.txt in the distributed notices and the [original project](https://www.mmdagent.jp/).

## English factory voice

English pronunciations use the Carnegie Mellon Pronouncing Dictionary. English phoneme samples are rendered from the CMU US KAL16 diphone voice in Flite 2.2. See CMUdict.txt and Flite.txt in the distributed notices.

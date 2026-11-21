# Integration checklist

## Before implementing import/export
- [ ] Obtain user-provided final **OSTF 5.0 release** (complete spec/schema/examples/package rules).
- [ ] Pin its exact revision/checksum and read every normative field.
- [ ] Determine licensing for references/samples before copying into repo.
- [ ] Define canonical score vs session preservation semantics and associated digest updates.
- [ ] Add conformance fixtures from the actual final release, never invented placeholder examples.

## Windows first
- [ ] CMake core test green
- [ ] JUCE GUI builds Windows x64
- [ ] JUCE GUI builds macOS (CI) and package is clearly unsigned preview
- [ ] OSTF text import with good/bad fixtures
- [ ] OSTFP package load/save based on official manifest
- [ ] Basic deterministic MIDI renderer, SMF Type 1 exporter
- [ ] VST3 scan/load and safe state persistence
- [ ] Split ASIO input / WASAPI output, clock compensation measured
- [ ] Mac Core Audio and AU verified by collaborator when available

## Publishing
- [ ] Choose source licence and settle JUCE/other dependency licence obligations
- [ ] Separate OSS/app source from third-party plugin binaries
- [ ] Windows installer upgrades preserve project and settings data
- [ ] macOS Developer ID signing/notarization when public distribution demands it
- [ ] Document incomplete features and real-world test coverage honestly

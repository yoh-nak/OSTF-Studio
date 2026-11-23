# OSTFP preview fixture

The legacy import tests cover standalone scores and conversion to Core 5.0.0. The 4.0.0 pickup and gate fixture is authored for Studio. The other fixtures are byte copies of the upstream examples below, renamed for version-based testing.

- 3.5.0 uses `47_document_branch_and_merge_identity.ostf` by yoh-nak, © 2026, under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). [Source](https://github.com/yoh-nak/Open-Song-Timeline-Format-3.5/tree/468ebdd/samples)
- 4.1.0 uses `80-combined-directions.ostf`, 4.2.0 uses `04-basic-vocal.ostf`, and 4.3.0 uses `04-aaba-performance-plan.ostf`. See [the upstream versions and notices](../../third_party/ostf-schemas/NOTICE.md).
- The packaged 4.3.0 score is the unchanged [06-legacy-4.3-compatible.ostfp example](https://github.com/yoh-nak/Open-Song-Timeline-Format-5.0/blob/d8ce48c/examples/ostfp/06-legacy-4.3-compatible.ostfp) from the CC0 OSTF 5.0 release.

`10-comprehensive-core.ostfp` is copied byte-for-byte from `examples/ostfp/` of the [OSTF 5.0.0 source release](https://github.com/yoh-nak/Open-Song-Timeline-Format-5.0/tree/d8ce48ca6c873a5a24a94bcc80d847bf33135b46). The upstream [license policy](https://github.com/yoh-nak/Open-Song-Timeline-Format-5.0/blob/d8ce48ca6c873a5a24a94bcc80d847bf33135b46/LICENSE-POLICY.md) applies CC0 1.0 to original examples. This package contains only a score, with no external audio or plugin payloads.

SHA-256 from upstream `SHA256SUMS.txt`

```text
c9b1227c12a65d828cea7b1adb3909432a69844f559da3b1a5f525157623763f
```
The `tinysoundfont-saw.sf2` fixture is the 484-byte `MinimalSoundFont` from
[TinySoundFont's example](https://github.com/schellingb/TinySoundFont/blob/a3621a19c6504885cd9583e6e26c35bc954fc8ec/examples/example1.c).
It is a single synthetic waveform used for channel tests. See the
[TinySoundFont license](../../third_party/tinysoundfont/LICENSE.txt).

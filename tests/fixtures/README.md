# OSTFP preview fixture

`10-comprehensive-core.ostfp` is copied byte-for-byte from `examples/ostfp/` of the [OSTF 5.0.0 source release](https://github.com/yoh-nak/Open-Song-Timeline-Format-5.0/tree/d8ce48ca6c873a5a24a94bcc80d847bf33135b46). The upstream [license policy](https://github.com/yoh-nak/Open-Song-Timeline-Format-5.0/blob/d8ce48ca6c873a5a24a94bcc80d847bf33135b46/LICENSE-POLICY.md) applies CC0 1.0 to original examples. This package contains only a score, with no external audio or plugin payloads.

SHA-256 from upstream `SHA256SUMS.txt`

```text
c9b1227c12a65d828cea7b1adb3909432a69844f559da3b1a5f525157623763f
```
The `tinysoundfont-saw.sf2` fixture is the 484-byte `MinimalSoundFont` from
[TinySoundFont's example](https://github.com/schellingb/TinySoundFont/blob/a3621a19c6504885cd9583e6e26c35bc954fc8ec/examples/example1.c).
It is a single synthetic waveform used for channel tests. See the
[TinySoundFont license](../../third_party/tinysoundfont/LICENSE.txt).

# SUPER KNOBBER REVERB

[English](super-knobber-reverb.en.md) · [図鑑の目次](README.md)

**用途と入出力**　モノ入力にはモノ出力、ステレオ入力にはステレオ出力で使えます。トラックのFX枠ではDry / Wetで原音と混ぜ、センド用リターンでは100% wetにして送り量を調整します。

![SUPER KNOBBER REVERBの操作画面](../images/plugins/super-knobber-reverb.png)

| 操作 | 聴きどころ・読み方 |
| --- | --- |
| 空間タイプ、素材、スタイル | 調整したい音場の出発点を選びます。短い打音だと残響の違いが分かりやすくなります。 |
| Pre Delay・Decay | 原音から残響が始まるまでの間隔と、残響が消えるまでの長さを調整します。 |
| Early Level・Tail Level | 最初に届く反射音と後続の余韻のバランスを取ります。 |
| 入出力のスペクトル・EQ比較 | 入力フィルターやポストEQを調整し、処理前後を比較します。 |
| WETピーク、RMS、相関 | ピークで瞬間的な大きさ、RMSで平均的な大きさ、相関で左右の似かよりを確認します。RT60の5帯域表示は設定上の減衰目標です。 |

曲を書き出す際は **曲末に残す余韻** を選び、最後の残響が収まる時間を確保します。[全コントロールの範囲と初期値](../../src/plugins/effects/knobber/Controls.h)

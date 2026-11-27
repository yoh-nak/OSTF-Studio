# 解析・素材の取り出し

[English](analysis.en.md) · [図鑑の目次](README.md) · [Studio操作マニュアル](../manual/README.md)

ミックスの状態を目で確かめたいとき、または録音から素材を取り出して編集したいときに使います。

[Wave Analyzer](#wave-analyzer) · [Stem Separator](#stem-separator)

## Wave Analyzer

波形、スペクトル、ステレオの広がり、ラウドネス、トゥルーピークを同時に確認できます。マスターの最後に挿して曲を再生し、気になる瞬間に **Freeze** で表示を止めます。

下の画像はステレオ信号の解析中です。上段でラウドネスとトゥルーピーク、中央で周波数の山と左右の広がり、下段で波形の時間変化を読めます。

![Wave Analyzerの画面。信号入力中のスペクトル、ステレオ分布、波形とラウドネス](../images/plugins/wave-analyzer.png)

## Stem Separator

録音からドラム、ベース、その他、ボーカルを推定してWAVに取り出します。ファイルをドロップするか、**Capture track** で区間を収録し、結果の **WAV** や **MIDI** をトラックへドラッグします。分離音には他のパートが残ることがあります。

![Stem Separatorの画面。オーディオの取り込みと推定ステムのWAV・MIDI出力](../images/plugins/stem-separator.png)

[プラグイン図鑑の目次に戻る](README.md)

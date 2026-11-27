# 作曲ツールと音源

[English](workstations.en.md) · [図鑑の目次](README.md) · [Studio操作マニュアル](../manual/README.md)

MIDIトラックに挿し、曲全体を鳴らす、歌詞を歌わせる、または演奏に音色を付けるためのプラグインです。

[OSTF Studio](#ostf-studio) · [OSTF Scale Editor](#ostf-scale-editor) · [GM Sound Module](#gm-sound-module) · [OSTF Formant Singer](#ostf-formant-singer) · [FM Synth](#fm-synth) · [Analog Synth](#analog-synth) · [Wavetable Synth](#wavetable-synth) · [Drum Machine](#drum-machine) · [Instrument Rack](#instrument-rack) · [SUPER KNOBBER LEAD](#super-knobber-lead)

## OSTF Studio

いつものDAWでOSTFの曲全体を演奏するワークステーションです。開発版のインストーラーで **OSTF Studio (VST3)** を選び、DAWの音源枠で開きます。まず「Paper Lantern Replies」を読み込み、ホストの再生ボタンで伴奏を鳴らしてください。

![OSTF Studioの画面。ホスト追従ボタン、アレンジ、ピアノロール](../images/plugins/ostf-studio.png)

[詳しい操作と入出力](ostf-studio.md)

## OSTF Scale Editor

公開アルファ2では、**OSTF Studio内の音源トラック**に **OSTF Scale Editor** をセットします。他のDAWでは、先に **OSTF Studio (VST3)** を立ち上げ、その中で曲を開きます。左ペインでコードを選び、候補を試聴して **スケールを設定** を押すと、左のコードに確定したスケール名とチェックが付きます。書き込み先が **和声** ならStudioのグローバルスケールレーンに、トラックならそのトラックのスケール指定に反映されます。曲はStudioまたは親DAWで保存します。

ヘッダーで試聴する構成を選んで **構成を試聴** を押します。**全曲を試聴** は曲頭から始まります。再生中のスケール名を見ながら、確定したスケールや未設定範囲の候補を聴けます。試聴音源はヘッダーで選びます。 [スケールの設定と試聴](../manual/README.md#scale-assignment)

![OSTF Scale Editorの画面。構成の選択、コード一覧とスケール比較](../images/plugins/ostf-scale-editor.png)

## GM Sound Module

1台で16のMIDIチャンネルを受けるGeneral MIDI音源です。ピアノとベースを別チャンネルに割り当て、各パートの音色を選ぶと、1つの音源でアンサンブルを鳴らせます。

![GM Sound Moduleの画面。MIDIパートとチャンネル別の設定](../images/plugins/gm-sound-module.png)

## OSTF Formant Singer

OSTFの旋律と歌詞を歌わせる音源です。普段のDAWでは **Load OSTF** で歌詞と音符のある曲を開き、歌唱パートを選んで再生します。長く伸ばす音で **Vibrato / scoop depth** の効果を聴き比べられます。

短いフレーズでは、表示歌詞に英単語、対応する発音歌詞にカタカナを入れたボーカルパートを試せます。デモの **Paper Lantern Replies** のコーラスで、短い子音のつながりと伸ばした母音を聴き比べられます。

![OSTF Formant Singerの画面。OSTFの曲、ボーカルパートとビブラートの深さ](../images/plugins/ostf-formant-singer.png)

## FM Synth

ベルやエレピのように打鍵の輪郭が立つFM音源です。同じ和音で **Glass Keys** と **Bell Cascade** を切り替え、余韻の違いを聴いてみてください。

![FM Synthの画面。Glass Keysプリセット、ToneとOutput](../images/plugins/fm-synth.png)

## Analog Synth

ベースからブラス系までをカバーするアナログ方式のシンセです。**Round Bass** を低い音域で弾き、**Twin Saw** に切り替えると倍音の違いが聴けます。

![Analog Synthの画面。Round Bassプリセット、ToneとOutput](../images/plugins/analog-synth.png)

## Wavetable Synth

波形を移り変わらせるパッドや短いスタブ向けの音源です。**Air Pad** で和音を伸ばして **Tone** を動かし、同じ和音を **Digital Stab** で短く刻んでみてください。

![Wavetable Synthの画面。Air Padプリセット、ToneとOutput](../images/plugins/wavetable-synth.png)

## Drum Machine

MIDIノートをキック、スネア、ハイハットなどに割り当てるドラム音源です。同じパターンを **Studio Kit** と **Tight Machine** で再生し、キットによる減衰の違いを比べられます。

![Drum Machineの画面。Studio Kitプリセットと出力フェーダー](../images/plugins/drum-machine.png)

## Instrument Rack

1つのMIDI演奏で複数の音源を重ねます。**+ Instrument layer** で音源を2つ追加し、それぞれの **Layer level** で、たとえばピアノに薄くパッドを重ねられます。

![Instrument Rackの画面。音源レイヤー、音量、インサートの操作欄](../images/plugins/instrument-rack.png)

## SUPER KNOBBER LEAD

M4L版LEAD v1.2の音色を、Studioや他のDAWのMIDIトラックで演奏できます。上部でジャンルとプリセットを選びます。たとえば **GM → GM 082 Lead 2 sawtooth** を選び、**FM / Table / Filter → Cutoff** を動かすと、原音の明るさを調整できます。ノブはDAWから直接オートメーションできます。

**Original rate** が初期設定です。高音で歪みを使うときは **2x oversampling** に切り替えて響きを比べられます。どちらもホストに報告するレイテンシーは16サンプルです。

![SUPER KNOBBER LEADの画面。オシレーターとミキサー、ジャンルと音色の選択](../images/plugins/super-knobber-lead.png)

[詳しい操作と入出力](super-knobber-lead.md)

[プラグイン図鑑の目次に戻る](README.md)

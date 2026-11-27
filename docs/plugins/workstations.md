# 作曲ツールと音源

[English](workstations.en.md) · [図鑑の目次](README.md) · [Studio操作マニュアル](../manual/README.md)

MIDIトラックに挿し、曲全体を鳴らす、歌詞を歌わせる、または演奏に音色を付けるためのプラグインです。

[OSTF Studio](#ostf-studio) · [OSTF Scale Editor](#ostf-scale-editor) · [GM Sound Module](#gm-sound-module) · [OSTF Formant Singer](#ostf-formant-singer) · [FM Synth](#fm-synth) · [Analog Synth](#analog-synth) · [Wavetable Synth](#wavetable-synth) · [Drum Machine](#drum-machine) · [Instrument Rack](#instrument-rack) · [SUPER KNOBBER LEAD](#super-knobber-lead)

## OSTF Studio

いつものDAWでOSTFの曲全体を演奏するワークステーションです。開発版のインストーラーで **OSTF Studio (VST3)** を選び、DAWの音源枠で開きます。まず「Paper Lantern Replies」を読み込み、ホストの再生ボタンで伴奏を鳴らしてください。

![OSTF Studioの操作画面](../images/plugins/ostf-studio.png)

## OSTF Scale Editor

開発版では、**OSTF Studio内の音源トラック**に **OSTF Scale Editor** をセットします。他のDAWでは、先に **OSTF Studio (VST3)** を立ち上げ、その中で曲を開きます。左ペインでコードを選び、候補を試聴して **スケールを設定** を押すと、左のコードに確定したスケール名とチェックが付きます。書き込み先が **和声** ならStudioのグローバルスケールレーンに、トラックならそのトラックのスケール指定に反映されます。曲はStudioまたは親DAWで保存します。

左のコードを選んで **構成を試聴** を押すと、その構成に設定済みのスケールが試聴音源で順に鳴ります。**全曲を試聴** は曲の先頭から再生します。**書き込み先** で和声またはトラックを選べます。MIDIノートの入力は不要です。ヘッダーの **停止** で試聴を終了します。

![OSTF Scale Editorの操作画面](../images/plugins/ostf-scale-editor.png)

## GM Sound Module

1台で16のMIDIチャンネルを受けるGeneral MIDI音源です。ピアノとベースを別チャンネルに割り当て、各パートの音色を選ぶと、1つの音源でアンサンブルを鳴らせます。

![GM Sound Moduleの操作画面](../images/plugins/gm-sound-module.png)

## OSTF Formant Singer

OSTFの旋律と歌詞を歌わせる音源です。普段のDAWでは **Load OSTF** で歌詞と音符のある曲を開き、歌唱パートを選んで再生します。長く伸ばす音で **Vibrato / scoop depth** の効果を聴き比べられます。

![OSTF Formant Singerの操作画面](../images/plugins/ostf-formant-singer.png)

## FM Synth

ベルやエレピのように打鍵の輪郭が立つFM音源です。同じ和音で **Glass Keys** と **Bell Cascade** を切り替え、余韻の違いを聴いてみてください。

![FM Synthの操作画面](../images/plugins/fm-synth.png)

## Analog Synth

ベースからブラス系までをカバーするアナログ方式のシンセです。**Round Bass** を低い音域で弾き、**Twin Saw** に切り替えると倍音の違いが聴けます。

![Analog Synthの操作画面](../images/plugins/analog-synth.png)

## Wavetable Synth

波形を移り変わらせるパッドや短いスタブ向けの音源です。**Air Pad** で和音を伸ばして **Tone** を動かし、同じ和音を **Digital Stab** で短く刻んでみてください。

![Wavetable Synthの操作画面](../images/plugins/wavetable-synth.png)

## Drum Machine

MIDIノートをキック、スネア、ハイハットなどに割り当てるドラム音源です。同じパターンを **Studio Kit** と **Tight Machine** で再生し、キットによる減衰の違いを比べられます。

![Drum Machineの操作画面](../images/plugins/drum-machine.png)

## Instrument Rack

1つのMIDI演奏で複数の音源を重ねます。**+ Instrument layer** で音源を2つ追加し、それぞれの **Layer level** で、たとえばピアノに薄くパッドを重ねられます。

![Instrument Rackの操作画面](../images/plugins/instrument-rack.png)

## SUPER KNOBBER LEAD

M4L版LEAD v1.2の音色を、Studioや他のDAWのMIDIトラックで演奏できます。上部でジャンルとプリセットを選びます。たとえば **GM → GM 082 Lead 2 sawtooth** を選び、**FM / Table / Filter → Cutoff** を動かすと、原音の明るさを調整できます。ノブはDAWから直接オートメーションできます。

**Original rate** が初期設定です。高音で歪みを使うときは **2x oversampling** に切り替えて響きを比べられます。どちらもホストに報告するレイテンシーは16サンプルです。

![SUPER KNOBBER LEADのオシレーターとミキサー](../images/plugins/super-knobber-lead.png)

[プラグイン図鑑の目次に戻る](README.md)

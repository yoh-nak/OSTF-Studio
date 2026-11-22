# OSTF Studio

[English](README.md) · **日本語**

**1.0.0 alpha 2 をダウンロード** · [Windows x64 インストーラー](https://github.com/yoh-nak/OSTF-Studio/releases/download/v1.0.0-alpha.2/OSTF_Studio_Windows_x64_Setup.exe) · [Windows Portable ZIP](https://github.com/yoh-nak/OSTF-Studio/releases/download/v1.0.0-alpha.2/OSTF_Studio_Windows_x64_Portable.zip) · [Apple silicon Mac 用 DMG](https://github.com/yoh-nak/OSTF-Studio/releases/download/v1.0.0-alpha.2/OSTF_Studio_macOS_arm64.dmg)

OSTF Studio は、楽曲の構造と演奏情報を OSTF 5.0 で扱う Windows・macOS 用の DAW です。現在は 1.0.0 のアルファ版を公開しています。同梱のデモ曲で、グローバルタイムライン、ピアノロール、VST3 音源とミキサーを試せます。

## ☕ Buy Me a Coffee で開発を応援する

デモ曲を聴いて「この DAW で曲を作ってみたい」と感じたら、コーヒー1杯分のご支援をいただけるとうれしいです。GM 音源の音づくりや、Windows・macOS の両方で曲を開いて再生できるようにするための開発・検証を続けています。**[Buy Me a Coffee で支援する](https://buymeacoffee.com/yohsukenak)**

## 開発版のトランスポートと曲情報

トランスポートには、小節・拍・ティックと30 fpsの経過タイムコードが並びます。テンポ・拍子・キー・スケール・コードは再生位置に追従し、小節途中のコードやテンポの変更も反映します。ピアノロールでは金色の再生線がノートとコントローラーレーンを横切り、拡大表示中は次の表示範囲へ自動で進みます。表示ツールバーの右側で曲名とファイル名を確認できます。

**ファイル → 曲情報を編集**で曲名、副題、説明、クレジットを設定できます。同じ人が作詞と作曲を担当した曲では、同じ名義で `lyricist` と `composer` の2行を追加します。**適用**のあと、OSTFまたはOSTFPで保存してください。

## 開発版のリードシート・楽譜・採譜

ビューバー左端の3つのアイコンで、メイン画面を **アレンジ・リードシート・楽譜** に切り替えます。「表示」メニューからも同じ画面を開けます。再生中は金色の線が演奏位置を示し、「再生に追従」がオンなら次の小節に表示が進みます。追従をオフにすると、再生を続けたまま別の箇所を読めます。

リードシートではコード・スケール・歌詞を個別に表示でき、1行の小節数を1・2・4・8・16から選べます。歌詞がある曲では歌詞パートと本文・発音の表示を選べます。発音の指定がない箇所には本文を表示します。楽譜ではトラックごとに大譜表、ト音記号、ハ音記号（アルト）、ヘ音記号、ギターTAB、ベースTAB、ドラム譜表を選べます。**PDF保存・印刷** で選択した内容をA4に整えて出力し、PDFビューアーから印刷できます。楽譜は **MusicXML** でも書き出せます。

**ファイル → 取り込み・採譜** から、MIDI演奏（SMF 0・1）またはWAV録音を別のOSTFプロジェクトとして保存できます。WAVの原音は楽譜と一緒に保管されます。**単旋律を採譜（試作）** は、2分以内の単音の録音から下書きのノートトラックを作ります。ピアノロールで結果を確認できます。これらは公開済みのalpha 2より後の開発版の機能です。

## インストール

Windows は Setup.exe を実行するか、Portable ZIP を展開してください。Apple silicon Mac は DMG を開き、中の **Install OSTF Studio and VST3.pkg** でアプリと VST3 をインストールします。macOS の Developer ID 署名・公証は未設定です。公開版のインストーラーと対応ソースは [GitHub Releases](https://github.com/yoh-nak/OSTF-Studio/releases) の各リリースの **Assets** にあります。

開発ビルドは **Actions → 各ビルドの実行結果 → Artifacts** に7日間保存されます。公開版の履歴・配布ファイルは Releases から取得できます。

## alpha 2 の新機能

- **ファイル → MIDI（SMF）で保存** で曲全体を `.mid` に書き出します。フォーマット0は MIDI チャンネルを保ったまま全パートを1トラックにまとめます。フォーマット1はテンポ・拍子のトラックと各パートの名前付きトラックに分けます。録音したノート、GM マッピングのあるドラム、プログラムチェンジ、CC、ピッチベンド、アフタータッチも対象です。WAV・プラグインの設定・歌唱曲線は OSTF プロジェクトに保存してください。コードから表示用に展開したボイシングを重複して書き出すことはありません。
- **設定 → 言語** で「OS の表示言語を使用」「English」「日本語」から選べます。変更は次回の起動時に反映します。初期設定では日本語環境なら日本語、それ以外の環境では英語で開きます。
- メニューバーの下のアイコンで、ピアノロール、ミキサー、プラグインブラウザー、出力トラックの表示を切り替えます。レーンのアイコンではグローバルタイムラインの表示項目を選べます。アイコンの上にポインターを置くと機能名が表示されます。

## 曲を開いて演奏する

**File → Demo songs** でデモを選ぶか、**Open OSTF / OSTFP** で手元の曲を開きます。セクション、コード、調、拍子、テンポとトラックのクリップが共通の小節位置に並びます。コードのボイシングはグローバルの1行に表示されます。楽器で実際に弾く伴奏パートはアレンジ側に残ります。[デモ曲と確認できる項目](examples/demo-songs/README.md)

トラックを選ぶとピアノロールに音高と長さが表示されます。曲に記録されたベロシティ、CC、アフタータッチ、ボーカルのピッチと表現曲線も確認できます。アレンジとピアノロールはそれぞれ横方向に拡大・縮小、スクロールできます。**Drag MIDI to another DAW** は選択した音階付きトラックを別の DAW へドラッグする機能です。

**Settings → Audio / MIDI Devices** で機器を選びます。Windows は WASAPI と ASIO、Apple silicon Mac は Core Audio に対応します。ASIO の利用には対応機器のドライバーが必要です。MIDI 入力を有効にし、音階付きトラックを選んで **Record MIDI** を押すと、停止時に録音したノートがクリップになります。オーディオ録音と歌声合成は開発予定です。

## 音源・エフェクト・ミキサー

開発ビルドには OSTF Studio 製のステレオVST3エフェクト9種を同梱します。**File → Demo songs → Paper Lantern Replies** を開くと、リードボーカルの **EQ → Compressor**、ドラムとベースの Bus A、伴奏の Bus B、Return 1 のリバーブを確認できます。トラックのセンド量、パン、フェーダー、エフェクトの順番を `.ostf` と `.ostfp` の両方で復元します。[デモ曲ごとの試聴手順](examples/demo-songs/README.md)

プラグインブラウザーには VST3 がメーカー別に表示されます。初回のスキャン結果はこの PC に保存されます。楽器とエフェクトを絞り込み、トラックへドラッグして割り当てられます。見つからない場合は **Scan one…** から個別に VST3 を選択できます。

**OSTF GM Sound Module** はチャンネルごとに音色、音量、パン、エクスプレッションなどを設定できる16パートの音源です。チャンネル10は打楽器です。配布版は FluidR3 GM を同梱しており、手元の SF2 も読み込めます。複数トラックで1つの音源を共有するには **Instrument → Instrument destination** で同じ音源トラックを指定し、各パートの MIDI チャンネルを選びます。共有音源のステレオ出力は音源を置いたトラックで調整します。[FluidR3 の配布条件](licenses/NOTICE.md)

**OSTF Arranger** は通常のインストゥルメントスロットに置きます。プラグイン内の **Choose instrument** で発音する VST3 音源を選びます。生成したパターンを Studio のトラックには OSTF ノートとして、外部 DAW には MIDI としてドラッグできます。外部 DAW ごとの動作検証は継続中です。

トラックの音量・パン・ミュート・ソロ、バスやリターンへの送りはミキサーと連動します。**FX** では最大8個のステレオ VST3 エフェクトを追加し、ドラッグまたは上下ボタンで処理順を変更できます。バス、リターン、マスターにも個別のフェーダー、ミュート、エフェクトがあります。

開発版では、各トラックの横メーターとミキサーの縦メーターで、エフェクト・パン・フェーダーを通った左右のピークを確認できます。バス、リターン、マスターの合成音と、出力3・4に送るキューも個別に表示します。0 dBFS に達すると赤いランプが約1秒点灯します。複数の MIDI トラックでひとつの音源を共有する場合は、その音源を挿したトラックに合成後のレベルが表示されます。

トラックの下端を上下にドラッグすると、その行の高さが変わります。ダブルクリックで折りたたみ、もう一度ダブルクリックすると元の高さに戻ります。広げた行では音量とパンを操作でき、ノート入りクリップには音程と長さが表示されます。下のトラックへは縦スクロールで移動できます。

## OSTF と OSTFP の保存

**Save as OSTF** はスコアと Studio のミキサー設定を保存します。リンクした WAV は隣の `<曲名>.media` にコピーし、相対パスで参照します。曲を移動するときはこのフォルダーも一緒に移動してください。**Save as OSTFP** はスコア、Production Session の設定、WAV を1ファイルにまとめます。現行プレビューは単一拍子・最大256小節・最大8アレンジトラックに対応しています。拍子の混在や規格全体の検証は今後の課題です。[公式仕様](https://github.com/yoh-nak/Open-Song-Timeline-Format-5.0/tree/d8ce48ca6c873a5a24a94bcc80d847bf33135b46)

Windows のインストーラーはファイルアイコンを登録し、同梱 VST3 を `%LOCALAPPDATA%\Programs\Common\VST3` に配置します。他の DAW で見つからない場合はこのパスを検索対象に加えて再スキャンしてください。

## 開発を再開する

リポジトリをクローンし、[開発ルール](AGENTS.md)、[引き継ぎ](HANDOFF.md)、[設計とロードマップ](docs/ARCHITECTURE.md) を読みます。C++20、CMake 3.22 以降、Git が必要です。Windows では Visual Studio 2022 と Windows SDK、macOS では Xcode Command Line Tools を使用します。

```sh
git clone https://github.com/yoh-nak/OSTF-Studio.git
cd OSTF-Studio
cmake -S . -B build-core -DOSTF_STUDIO_BUILD_GUI=OFF
cmake --build build-core --config Release
ctest --test-dir build-core -C Release --output-on-failure --no-tests=error
```

GUI をビルドするには `build-core` を `build` に、`OSTF_STUDIO_BUILD_GUI=OFF` を `ON` に変更します。GUI の構成時に JUCE 9.0.5 を取得します。配布版と同じ GM 音源を使用する場合は先に `python packaging/prepare_fluid_r3.py` を実行してください。Windows x64 と macOS arm64 の CI がビルドと自動テストを行います。実機のオーディオ、外部プラグインとの連携は別途検証します。[ビルドと配布の手順](docs/RELEASING.md)

## ライセンス

OSTF Studio 本体と同梱プラグインの独自コードは GPL-3.0-only です。ASIO SDK は GPLv3、JUCE は AGPLv3 の条件で配布しています。各 Release に対応するソースとビルドスクリプトを同梱します。[GPLv3 全文](LICENSE) · [依存ライブラリのライセンス](licenses/NOTICE.md)

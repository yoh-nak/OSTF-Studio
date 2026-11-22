# OSTF Studio デモ曲ギャラリー

OSTF Studio向けに作曲した5曲です。各 `.ostf` は楽譜データ、対応する `.ostfp` は同じデータとマニフェストを含むプロジェクトです。OSTFPのProduction Sessionに、各演奏トラックの通常のVST3 InstrumentとしてOSTF GM Sound Moduleを記録しています。MIDIチャンネルはOSTF Coreの track.midi、音色番号は各トラックのProgram Changeイベントに入っています。GM音源と試聴バンクはアプリの配布パッケージに含まれます。 [OSTF 5.0.0の仕様とSchema](https://github.com/yoh-nak/Open-Song-Timeline-Format-5.0/tree/d8ce48ca6c873a5a24a94bcc80d847bf33135b46)

| 曲 | 尺・トラック数 | 曲の構成と確認できるデータ |
|---|---:|---|
| [Neon Platforms](neon-platforms.ostfp) · シティポップ | 2:52 · 76小節 · 8 | Aメロ、プレコーラス、サビ、ブリッジ、ギター間奏、終サビ、アウトロ。ギターのフックに弦、ブラス、パッド、カッティングを段階的に重ねます。 |
| [Blue Hour Waltz](blue-hour-waltz.ostfp) · ジャズワルツ | 2:55 · 128小節 · 7 | ピアノイントロ、テーマ、ピアノとホーンのコーラス、4小節単位のセクション、テーマ回帰。3拍のウォーキングベースとライド、ブラシのスロット、固定とオープンの演奏プランを収録。 |
| [Relay Circuit](relay-circuit.ostfp) · テクノ | 3:00 · 96小節 · 8 | DJイントロ、グルーヴ、ブレイク、ビルド、2回のドロップ、DJアウトロ。サブベース、コードスタブ、金属的なシーケンス、パッドが区間ごとに入れ替わります。 |
| [Paper Lantern Replies](paper-lantern-replies.ostfp) · R&B | 2:52 · 56小節 · 8 | 2つのヴァース、プレコーラス、サビ、ブリッジ、終サビ。英語のオリジナル歌詞にリードとコーラスの譜面を割り当て、音節とノートを対応付けています。 |
| [Offset Bridges](offset-bridges.ostfp) · プログレッシブ・ロック | 2:22 · 80小節 · 8 | 7/8のテーマと4/4の展開、混合拍子の間奏、再現部、コーダ。ギター、オルガン、メロトロン、ロックドラムを収録。混合拍子の検証曲です。 |

**Paper Lantern Replies** の歌詞にはヴァースごとの文章と繰り返すサビがあります。

> Fold a little dawn into the blue
> Leave a little light across the room

リードの音節とノートは全歌唱セクションで対応しています。サビには「Ah」のバックコーラスが付きます。リードにはスクープ、ビブラート、息継ぎ、ピッチカーブと、dynamics・breathiness・tension・brightness・voicingの推移を入れています。CC11、アフタータッチ、ピッチベンドも記録しています。尺は楽譜上のテンポから計算した概算です。


## Studioで確認する

- **Chord Voicing** はコードに保存された構成音の表示です。**Electric Piano Comp**、**Acoustic Piano Comp**、**Warm Electric Piano** などは実際に弾くパートで、アレンジ側に表示されます。
- **File → Demo songs** から4曲を開けます。フォルダー内の `.ostf`／`.ostfp` も **File → Open OSTF / OSTFP** で開けます。現行プレビューは混合拍子の **Offset Bridges** を読み込み時に明示的に拒否します。
- **View → Global Timeline Lanes** でセクション、コード、ボイシング、スケールなどの表示を切り替えます。アレンジの区切りは名前付きクリップで確認できます。
- **Paper Lantern Replies** の **Lead Vocal Guide** を選び、ピアノロールのパラメーター選択で、ベロシティ、CC11、チャンネル／ポリ・アフタータッチ、歌唱ピッチ、dynamics・breathiness・tension・brightness・voicingを切り替えます。
- 試聴するには、**Settings → Audio / MIDI Devices** で出力を確認して **Play** を押します。デモを開くと、保存した通常のInstrument割り当てが復元されます。読み込み後にPlayでCoreのProgram Changeが各チャンネルに送られます。単体OSTFはStudioが保存する共通の制作設定を使用します。ドラムはGMスロットのマッピングを使います。ボーカルも試聴用の楽器音色で鳴ります。歌唱合成、歌詞の表示、演奏プラン、発音確率、MIDIピッチベンドの再生は未対応です。

アレンジとピアノロールはそれぞれ下部の表示小節数と横スクロールで拡大・移動できます。コードの切り替わりは拍単位で表示します。尺と拍子を保ちつつ、イントロでは和音を長めに保ち、区切りでは着地する和音を前倒ししています。

## 再生成と検証

### ミックスを聴き比べる

各演奏トラックの音源は OSTF GM Sound Module 1台です。FX は音源の後に並び、一覧の上から順に音声を処理します。各曲で音量とパンを設定し、ドラムやベースを **Bus A**、伴奏を **Bus B** にまとめています。両バスと **Return 1** は Master に戻ります。

| 曲 | 開いて確認する箇所 |
|---|---|
| Paper Lantern Replies | リードボーカルの FX は **EQ → Compressor**。リードから Return 1 へ −13 dB、バックコーラスから −11 dB で送ります。ボーカルガイドの音色で違いを確認できます。 |
| Blue Hour Waltz | 冒頭のピアノの出力は Bus B、ルームへのセンドは −14 dB。ピアノをソロにして再生し、Bus B をミュートすると残響だけ、Return 1 をミュートすると直接音だけを確認できます。 |
| Neon Platforms | ギターフックの **Guitar Amp → EQ**、リズムバスのコンプレッサー、マスターのリミッターを確認できます。 |
| Relay Circuit | サブベースの **Distortion → EQ** とシンセのモジュレーションを確認できます。サブベースには残響を送りません。 |
| Offset Bridges | リードの **Distortion → Guitar Amp** を含む変拍子のファイル検証用デモです。 |

**Return 1** の OSTF Reverb は初期値がウェット100%です。各トラックの送り量を上げると残響が増えます。センドはポストフェーダーなので、トラックフェーダーを下げると送りも下がります。FX ウィンドウでは挿入順の変更、パラメーター編集ができます。個々のVST3は **Plug-ins → Effects → OSTF Studio** から追加できます。

固定した公式仕様のチェックアウトを用意し、検証用Python環境に `jsonschema==4.25.1` をインストールして `python examples/demo-songs/verify.py <仕様リポジトリのパス>` を実行します。5曲について Score・Manifest・Production Session の各Schema、SHA-256、デバイスの参照と順序、バス・センドの接続、単体版と同梱版の一致を確認します。9種類のVST3の音声処理とバス／センドの実際の出力はGUIビルドのCTestで検証します。

リポジトリのルートで `python examples/demo-songs/generate.py` を実行します。生成はPython標準ライブラリのみを使います。ZIPの先頭は非圧縮の `mimetype` で、パッケージ中のスコアは対応する `.ostf` と同一バイトです。検証には固定リビジョンの公式Score／Manifest Schemaと `tools/ostfp.py validate` を使います。GUI有効ビルドの `ostf_studio_demo_gallery` は4曲の読み込み、歌唱レーンの値、混合拍子の拒否を確認します。

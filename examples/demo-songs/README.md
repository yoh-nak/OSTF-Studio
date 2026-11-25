# OSTF Studio デモ曲ギャラリー

OSTF Studio向けに作曲した7曲です。各 `.ostf` は楽譜データ、対応する `.ostfp` は同じデータとマニフェストを含むプロジェクトです。OSTFPのProduction Sessionに、各演奏トラックのVST3音源を記録しています。MIDIチャンネルはOSTF Coreの track.midi、音色番号は各トラックのProgram Changeイベントに入っています。GM音源と試聴バンクはアプリの配布パッケージに含まれます。 [OSTF 5.1.0 公式仕様](https://github.com/yoh-nak/Open-Song-Timeline-Format-5.1/tree/957bd071cecc58a12f417ad212c40723ba9812ca)

「Blue Hour Waltz」には共有音源版もあります。[Blue Hour Waltz (Shared GM)](blue-hour-waltz-shared-gm.ostfp) はピアノに置いた1台のGM音源を7パートで共有します。ピアノはCh 2、ベースはCh 3、ドラムはCh 10です。各パートの音量とパンはMIDIで指定し、ピアノのトラックにステレオ音声を返します。

| 曲 | 尺・トラック数 | 曲の構成と確認できるデータ |
|---|---:|---|
| [Neon Platforms](neon-platforms.ostfp) · シティポップ | 2:52 · 76小節 · 8 | Aメロ、プレコーラス、サビ、ブリッジ、ギター間奏、終サビ、アウトロ。ギターのフックに弦、ブラス、パッド、カッティングを段階的に重ねます。 |
| [Blue Hour Waltz](blue-hour-waltz.ostfp) · ジャズワルツ | 2:55 · 128小節 · 7 | ピアノイントロ、テーマ、ピアノとホーンのコーラス、4小節単位のセクション、テーマ回帰。3拍のウォーキングベースとライド、ブラシのスロット、固定とオープンの演奏プランを収録。 |
| [Relay Circuit](relay-circuit.ostfp) · テクノ | 3:00 · 96小節 · 8 | DJイントロ、グルーヴ、ブレイク、ビルド、2回のドロップ、DJアウトロ。サブベース、コードスタブ、金属的なシーケンス、パッドが区間ごとに入れ替わります。 |
| [Paper Lantern Replies](paper-lantern-replies.ostfp) · R&B | 2:52 · 56小節 · 9 | 5小節目から日本語のリードが入ります。1番と2番のヴァースは日本語、プレコーラスとサビは英語です。主唱194音とコーラス10音を、それぞれのOSTF Formant Singerで歌います。 |
| [Copper Mile](copper-mile.ostfp) · ロック | 2:12 · 64小節 · 6 | パワーコードの刻み、クリーンギターとオルガン、リードギターのソロを聴き比べられます。 |
| [First Light Vector](first-light-vector.ostfp) · トランス | 2:47 · 96小節 · 7 | 四つ打ちキックと裏拍のサブベースに16分アルペジオを重ね、ブレイクからドロップへ展開します。8小節目の3拍目から「Transition Sweep」をソロにすると、1小節のワープを3回聴き比べられます。 |
| [Offset Bridges](offset-bridges.ostfp) · プログレッシブ・ロック | 80小節 · 8 | 7/8のテーマを2+2+3と3+2+2のアクセントで展開します。ギターの掛け合いにオルガン、ウェーブテーブルの弦、ロックドラムを重ねます。 |

**Paper Lantern Replies** の1番は「あめのおとがする／まどにひかるしずく／そっとてをかさね／きょうもともるあかり」です。促音「っ」の間と「きょ」の発音を、リードのソロで聴き比べられます。サビの歌詞は次のように始まります。

> Fold a little dawn into the blue
> Leave a little light across the room

リードの音節とノートは全歌唱セクションで対応しています。サビには「Ah」のバックコーラスが付きます。リードにはスクープ、ビブラート、息継ぎ、ピッチカーブと、dynamics・breathiness・tension・brightness・voicingの推移を入れています。CC11、アフタータッチ、ピッチベンドも記録しています。尺は楽譜上のテンポから計算した概算です。


## Studioで確認する

- **Chord Voicing** はコードに保存された構成音の表示です。**Electric Piano Comp**、**Acoustic Piano Comp**、**Warm Electric Piano** などは実際に弾くパートで、アレンジ側に表示されます。
- **File → Demo songs** から7曲・8プロジェクトを開けます。
- **View → Global Timeline Lanes** でセクション、コード、ボイシング、スケールなどの表示を切り替えます。アレンジの区切りは名前付きクリップで確認できます。
- **Paper Lantern Replies** の **Lead Vocal** を選び、ピアノロールのパラメーター選択で、ベロシティ、CC11、チャンネル／ポリ・アフタータッチ、歌唱ピッチ、dynamics・breathiness・tension・brightness・voicingを切り替えます。
- 試聴するには、**Settings → Audio / MIDI Devices** で出力を確認して **Play** を押します。デモを開くと保存した音源の割り当てが復元されます。Paper Lantern Repliesの主唱は「歌い出しへ」で5小節目に移動し、歌詞の読みと音符を受け取って歌います。Singerの **Load voice folder...** から自分の録音やUTAUの日本語単独音を選ぶと、同じフレーズを別の声で確認できます。

アレンジとピアノロールはそれぞれ下部の表示小節数と横スクロールで拡大・移動できます。コードの切り替わりは拍単位で表示します。尺と拍子を保ちつつ、イントロでは和音を長めに保ち、区切りでは着地する和音を前倒ししています。

## 再生成と検証

### ミックスを聴き比べる

音源の音色はProgram Change、インサートの初期パラメーターはProduction Session 0.4のデバイス・オートメーションで指定しています。FXの設定はプロジェクトを開くと復元されます。

| 曲 | 聴き比べる箇所 |
|---|---|
| Paper Lantern Replies | FMのエレピ、ナイロンギターのAcoustic Guitar Rig、ウェーブテーブルの弦。主唱にDe-EsserとCompressorを挿し、Roomへの送りを−22 dBに設定。 |
| Blue Hour Waltz | 通常版のビブラフォンはFM音源。共有GM版では同じ編成を1台の音源で演奏。 |
| Neon Platforms | FMの鍵盤、ドラムマシン、ウェーブテーブルのパッド。サビではGMのブラスと弦がギターに応答。 |
| Relay Circuit | AnalogのサブベースにBass RigとParametric EQ。スタブと空間系パートを別々のウェーブテーブル音色で演奏。 |
| Copper Mile | リードのElectric Guitar Rigとテンポに合わせたDelay、オルガンのFM音色。 |
| First Light Vector | アルペジオ、サブ、パッド、ドラムを別音源で鳴らし、49小節目で音色が変化。 |
| Offset Bridges | 前半と展開部で7/8のアクセントを切り替え、FMパルスとウェーブテーブルの弦を重ねる。 |

Neon Platforms、Blue Hour Waltz、Relay Circuitでは、構成のテイクメニューから別の演奏を採用できます。

固定した公式仕様のチェックアウトを用意し、検証用Python環境に `jsonschema==4.25.1` をインストールして `python examples/demo-songs/verify.py <仕様リポジトリのパス>` を実行します。7曲と空の初期スコアについて Score・Manifest・Production Session の各Schema、SHA-256、デバイスの参照と順序、バス・センドの接続、単体版と同梱版の一致を確認します。9種類のVST3の音声処理とバス／センドの実際の出力はGUIビルドのCTestで検証します。

リポジトリのルートで `python examples/demo-songs/generate.py` を実行します。生成はPython標準ライブラリのみを使います。ZIPの先頭は非圧縮の `mimetype` で、パッケージ中のスコアは対応する `.ostf` と同一バイトです。検証には固定リビジョンの公式Score／Manifest Schemaと `tools/ostfp.py validate` を使います。GUI有効ビルドの `ostf_studio_demo_gallery` は各曲の読み込み、歌唱レーンの値、ミックス設定の往復を確認します。

## オーディオトラックの試聴

「Paper Lantern Replies」の **Glass Lantern Chimes** は1小節目の2拍目、「First Light Vector」の **Transition Sweep** は8小節目の3拍目から鳴ります。各トラックをソロにし、FXのEQ／コンプレッサー、出力先のHarmonyバス、Roomリターンへのセンドを個別に確認できます。

クラブ系の2曲には、曲のテンポとコードに合わせて作ったステレオWAVも収録しています。小節番号は画面と同じ1始まりです。

| 曲 | オーディオトラックを聴ける区間 |
|---|---|
| Relay Circuit（テクノ） | 9〜24小節の Swinging Hat Loop、37〜60小節の Foundry Toms、45〜60小節の Tape Dub Chords、69〜92小節の Warehouse Claps。45小節からはリズムバスのタムとハーモニーバスのコードが重なります。 |
| First Light Vector（トランス） | スイープに加え、9〜24小節に Air Shaker Loop、25〜56小節に Glass Pulse Gate、49〜64小節に Stereo Bell Echo、73〜92小節に Lift Handclaps が入ります。49小節でゲート音とベルを交互にソロにすると違いを聴けます。 |

ピアノロールまたはアレンジの小節ルーラーをクリックすると、その位置から試聴できます。オーディオトラックにはWAVの左右の波形が表示されます。全素材は生成スクリプトで合成したオリジナルです。単体の `.ostf` では隣の `assets/` フォルダーを一緒に置いてください。`.ostfp` には同じWAVとProduction Sessionのバインディングを同梱しています。

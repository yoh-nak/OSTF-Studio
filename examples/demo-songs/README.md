# OSTF Studio デモ曲ギャラリー

OSTF Studio向けに作曲した5曲です。各 `.ostf` は楽譜データ、対応する `.ostfp` は同じデータとマニフェストを含むプロジェクトです。音源や音声素材は同梱していません。 [OSTF 5.0.0の仕様とSchema](https://github.com/yoh-nak/Open-Song-Timeline-Format-5.0/tree/d8ce48ca6c873a5a24a94bcc80d847bf33135b46)

| 曲 | アレンジと聴きどころ | 確認できるデータ |
|---|---|---|
| [Neon Platforms](neon-platforms.ostfp) · シティポップ · 8小節 | 裏拍から始まるギターのモチーフを後半で高く展開し、A7からDm9を経てCmaj9に着地 | 4/4、C Ionian、転回形のボイシング、声部別ベロシティ、音高付きベース、GMマッピング付きドラムスロット |
| [Blue Hour Waltz](blue-hour-waltz.ostfp) · ジャズワルツ · 8小節 | 3連系のホーンの応答、3拍歩くベース、Fmaj7からFm6への変化 | 3/4、確率、ベロシティ変化とリリース、フレージング指示、固定ヘッドとオープンソロの有限演奏プラン |
| [Relay Circuit](relay-circuit.ostfp) · テクノ · 12小節 | 四つ打ちと裏拍のベース。ブレイクでキックとベースを抜き、最後に高いアルペジオとキックが戻る | 128 BPM、反復の演奏指示、無効ノートと発音確率、オープンハット、クラップ、タムのスロット |
| [Paper Lantern Replies](paper-lantern-replies.ostfp) · R&B · 8小節 | 前半の歌唱ガイドに後半のエレクトリックピアノが応答。E7からAm9へ解決 | 歌詞単位とノートの対応、歌詞候補、CC11、チャンネル／ポリ・アフタータッチ、MIDIピッチベンド、歌唱ピッチ、5種の表情カーブ、スクープとブレス |
| [Offset Bridges](offset-bridges.ostfp) · プログレッシブ・ロック · 8小節 | 2+2+3の刻みから4/4の長いアクセント付きリフへ移る | 7/8から4/4、116→138 BPM。混合拍子のインポート対応を確認するための検証曲 |

## Studioで確認する

- **File → Demo songs** から4曲を開けます。フォルダー内の `.ostf`／`.ostfp` も **File → Open OSTF / OSTFP** で開けます。現行プレビューは混合拍子の **Offset Bridges** を読み込み時に明示的に拒否します。
- **View → Global Timeline Lanes** でセクション、コード、ボイシング、スケールなどの表示を切り替えます。アレンジの区切りは名前付きクリップで確認できます。
- **Paper Lantern Replies** の **Lead Vocal Guide** を選び、ピアノロールのパラメーター選択で、ベロシティ、CC11、チャンネル／ポリ・アフタータッチ、歌唱ピッチ、dynamics・breathiness・tension・brightness・voicingを切り替えます。
- 音高付きノートを試聴するには、**Settings → Audio / MIDI Devices** で出力を設定し、各トラックに対応するVST3音源を割り当てて再生します。このデモ集では、ドラムスロットの発音、歌唱合成、歌詞の表示、演奏プラン、発音確率、MIDIピッチベンドなどはファイル内の検証用データです。Studioでの再生対応は機能ごとに異なります。

## 再生成と検証

リポジトリのルートで `python examples/demo-songs/generate.py` を実行します。生成はPython標準ライブラリのみを使います。ZIPの先頭は非圧縮の `mimetype` で、パッケージ中のスコアは対応する `.ostf` と同一バイトです。検証には固定リビジョンの公式Score／Manifest Schemaと `tools/ostfp.py validate` を使います。GUI有効ビルドの `ostf_studio_demo_gallery` は4曲の読み込み、歌唱レーンの値、混合拍子の拒否を確認します。

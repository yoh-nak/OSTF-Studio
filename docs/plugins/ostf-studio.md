# OSTF Studio

[English](ostf-studio.en.md) · [図鑑の目次](README.md)

**用途と入出力**　外部DAWのインストゥルメントトラックに読み込みます。ホストからMIDIと再生位置を受け、Studio内の楽曲をステレオで返します。音声入力・MIDI出力はありません。

![OSTF Studioの操作画面](../images/plugins/ostf-studio.png)

1. DAWのVST3音源一覧から **OSTF Studio** を選び、Studio内のファイルメニューでOSTFPを開きます。
2. 左上のホスト追従を有効にしてDAWを再生します。Studio内蔵音源とFXを含んだ音がDAWのトラックへ戻ります。Studio内で個別に試聴するときは追従を解除します。
3. コードにスケールを付けるには、Studio内の音源枠から **OSTF Scale Editor** を開きます。プロジェクト全体と追従設定は、ホストのプロジェクトを保存すると復元できます。

小節番号を一致させるには、曲頭の位置とテンポ・拍子マップを両方で揃えます。別のPCで同じ音を出すには曲で使う音源、FX、外部音色ライブラリーも必要です。

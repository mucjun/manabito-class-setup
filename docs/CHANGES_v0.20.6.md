# v0.20.6（2026-11-23）Windows セキュリティの除外を加えるバッチ

淳さんの要望：ダウンロードする ZIP に、Windows セキュリティでこのアプリを除外するバッチファイルを入れる。

- ZIP のいちばん上に defender_exclude_add.cmd（加える）・defender_exclude_remove.cmd（外す）。中身は scripts\defender_exclusion.ps1。
  英数字だけのバッチから PowerShell の部品を呼ぶ（GitHub のビルドで文字化けしないように）。BUILD_WINDOWS.cmd で extras\*.cmd を出力フォルダーにコピーする。
- 除外するのは Program Files のこのアプリの EXE 1 つだけ（前回の検出も EXE）。フォルダー全体・プロセス単位の除外はしない
  （プロセス単位にすると、そのプロセスが開くファイルまで調べなくなるため）。
- 管理者として開き直し、内容を表示して、Y のときだけ行う。EXE がまだ Program Files に無いときは何もしない。加えた・外したあと、本当にそうなったかを確かめる。
- README に、手で除外を加える手順。

## 注意

除外を加えるスクリプトは悪いプログラムもよく使う手口なので、バッチ自体が Defender に疑われることがある。その場合は README の手での手順を使う。

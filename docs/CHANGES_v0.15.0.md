# v0.15.0（2026-11-20）Creative Cloud（ランチャー）の LAN 配布

淳さんの要望。Adobe CC は個人版（受講生ごとのアカウント）のため、アプリ本体は配れない。ランチャーだけを Office と同じ形で配る。
暗号化された通信をのぞいてキャッシュする方法（全 PC に自前の証明書を入れる）は、受講生の通信すべてをほどける状態にするため採らない。

- 配布サーバーのインストール元を、Office と Adobe の 2 種類を扱う形に作り直した（src\share.cpp の Package）。
  UDP の答え：|office=版|バイト|adobe=版|バイト。TCP：ADOBE（一覧）・AGET（中身）を追加。
- 配布の窓に［Creative Cloud（ランチャー）を用意する］と、その進み具合・状態の行。
- scripts\adobe_source.ps1：Adobe の公式ページ（direct links）を読み、Windows（64 ビット）用のいちばん新しい ACCCx…zip を選んで入手。
  https の *.adobe.com のものだけ使う。ZIP を展開して Set-up.exe の署名を確かめてから置く（AdobeSource.new から入れ替え）。
- 受け取る側（Install-AdobeCC、手順 5 つ）：LAN を探す → 受け取る（無ければ今までどおり小さなインストーラーを入手）→ 署名の確認 → インストール → 仕上げ。
  受け取った ZIP は展開して、中の .exe と .dll がすべて Adobe の署名つきかを確かめる。Set-up.exe は自分のフォルダーで動かす。終わったら消す。
- LAN の受け取りを Office・Adobe 共通に（Find-LanPackageServer・Receive-LanPackage・Test-LanRelPath）。
- プロファイルに adobeLanSource（標準 true）。
- 検証：公式ページのリンクから Windows 64 ビット用の最新版（6.11.0）を選ぶ（ARM 用・古い版・偽のサイトは選ばない）。
  リンクの確かめ（http・紛らわしい名前のサイト・ZIP 以外は使わない）。代役のサーバーで Office と Adobe を出し分けて見つけ、Adobe を受け取り展開。
  Adobe のファイルを Office として受け取ろうとすると断る。

## 実機で確かめること

- ［Creative Cloud（ランチャー）を用意する］で ZIP が入手でき、版が表示されるか（Adobe のページの作りが変わると見つからなくなる）。
- 受け取る側で、ZIP の中の .exe・.dll がすべて Adobe の署名つきか（署名のないものが入っていると、LAN からの受け取りをやめてインターネットから入れる）。
- ZIP から展開した Set-up.exe が --silent で動くか。

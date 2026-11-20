# まなびと講習準備（v0.15.0）

講習で使う Windows 11 の PC を点検し、講習用の設定にそろえるアプリです。

- **まとめて実行**（ワンクリック）：設定をそろえる → アプリの削除とインストール（トグルのとおり）→ 前の受講生の履歴を消す、を 1 回の管理者の確認で続けて行う（scripts\oneclick.ps1）。
  履歴を消すのを最後にして、インストールでできたファイルや履歴もまとめて片付ける。進み具合は作業ごとの行で表示。取り消しもできる。
- **インターネットの確認**：起動時と、アプリの入手を伴う作業の前に、つながっていなければ「つないでください」と出す（Wi-Fi の一覧を開ける）。
  起動時は［つながずに続ける］も選べる。
- **点検する**：見るだけ。PC の設定は何も変えない。管理者権限は要らない。
- **設定をそろえる**：元に戻せる設定だけを変える（履歴やファイルは消さない）。管理者の確認が出る。終わると自動で点検し直す。
- **履歴を消す**：前の受講生の履歴とファイルを消す（元に戻せない）。管理者の確認が出る。終わると自動で点検し直す。
  このアプリが入っているフォルダー、OneDrive と同期しているフォルダー、クイック アクセスのピン留めは残す。
- **時間のかかる準備**：Office・Adobe CC デスクトップアプリ・独自アプリをトグルで選び、［今すぐ実行］か［時間を指定して実行］。
  管理者の確認は押したときの 1 回だけ（指定時刻まで、管理者として動く PowerShell が待つ）。待っている間と作業中はスリープさせない。
  ［時間を指定して実行］は作業中［取り消す］に変わる。電池で動いているときは始めない。
- **起動時の案内**：スマート アプリ コントロールが「評価中」か「オン」のとき、オフにするかを聞く（「次回から表示しない」を選べる）。
  オフにするのは `sac_off.ps1`（管理者）。自動でオフにできなかったときは「Windows セキュリティ」の画面を開く。
  ※「オン」の PC ではこのアプリ自体が起動できないため、実際に案内が出るのは主に「評価中」の PC。
  ※ 一度オフにすると、Windows の版によっては初期化するまでオンに戻せない。

画面（`ManabitoClassSetup.exe`、Win32）は殻だけで、中身は PowerShell の部品（`scripts\`）が行います。

## 使い方

1. `ManabitoClassSetup.exe` と同じフォルダーに `scripts` と `profiles` があることを確かめる。
2. EXE を起動し、「講習プロファイル」を選んで［点検する］（F5 でも可）。
3. 一覧の行を選ぶと、下の「説明」に詳しい内容が出る。
4. ［記録のフォルダー］で、点検の記録（`%LOCALAPPDATA%\ManabitoClassSetup\logs`）を開く。

- 点検は管理者権限なしで動きます。「そろえる」は `apply.ps1` だけを管理者として起動します。
- 「そろえる」は、画面を開いた利用者と、管理者として動いた利用者が違うときは中止します（別の人の設定を変えないため）。
- スマート アプリ コントロールが「オン」の PC では EXE が起動できません（署名なしのため）。
- 判定：そろっている／違う／一部異なる（黄色い帯）／確認できない／情報／対象外。
  - 一部異なる：ダウンロード・デスクトップに残っているのが、このアプリのフォルダーだけのとき（［履歴を消す］でも残すため）。
  - Office・Adobe の判定はトグルに従う（オン＝入っているべき、オフ＝入っていないべき）。トグルを押すと、その場で判定が変わる。
  - 時間のかかる準備が終わると、トグルをその PC の実際の状態（入っている＝オン）にそろえ、続けて点検し直す。

## 「そろえる」で変えるもの

| 項目 | やり方 | 元に戻すには |
|---|---|---|
| スリープ・画面の消灯・休止 | `powercfg /change ... 0`（電源接続時・電池とも） | 設定アプリの「電源」 |
| ふたを閉じたとき（ノート PC） | 電源接続時は「何もしない」 | コントロール パネルの「電源オプション」 |
| デスクトップの背景 | 画像を外し、無地の色（プロファイルの色） | 設定アプリの「個人用設定」 |
| ファイルの拡張子 | `HideFileExt` | エクスプローラーの「表示」 |
| タスクバーのウィジェット | ポリシー `HKLM\SOFTWARE\Policies\Microsoft\Dsh` の `AllowNewsAndInterests=0` | その値を消す |
| タスクバーの検索 | `SearchboxTaskbarMode` | 設定アプリの「タスクバー」 |
| 通知 | `ToastEnabled=0` | 設定アプリの「通知」 |
| OneDrive の自動起動 | 自動起動の登録（HKCU の Run の OneDrive）を外す。元の値は `HKCU\Software\ManabitoClassSetup` の `OneDriveRun` に控える | OneDrive の設定で「サインイン時に自動的に開始」をオン |
| 音量 | 消音 | 音量のアイコン |
| Windows Update（stop） | Pro/Enterprise：ポリシー `NoAutoUpdate=1`・`NoAutoRebootWithLoggedOnUsers=1`。Home：一時停止 35 日 | プロファイルを `run` にしてそろえる |

タスクバーと拡張子を変えたときは、エクスプローラーを起動し直します（開いているフォルダーの窓は閉じます）。
画面の解像度（変えない方針）・アプリの導入は、点検だけで変えません。

## 「履歴を消す」で消すもの

項目ごとに処理し、消せないものがあっても、消せるものは消す。消せなかったものだけを最後に一覧で知らせる。
全体を止めるのは「別の利用者で管理者になったとき」と「管理者として動いていないとき」だけ。
アプリの場所が分からないときは、ダウンロードとデスクトップだけ消さずにエラーとして知らせる。

| 項目 | 消すもの | 残すもの・注意 |
|---|---|---|
| 最近使ったファイル | Recent の .lnk、ジャンプリスト | クイック アクセスのピン留め（f01b4d95cf55d32a…）は残す |
| 入力履歴 | エクスプローラーのアドレス欄、「ファイル名を指定して実行」 | |
| Office | Word・Excel・PowerPoint の最近使ったファイルとフォルダー | 開いているときは消さずに「失敗」 |
| メモ帳 | 開いたままのタブ（TabState・WindowState） | 開いているときは消さずに「失敗」 |
| IME | 学習（Cache）、入力履歴（JpnIHDS.dat）、ユーザー辞書（imjp15cu.dic と _bak） | 使用中のものは次の再起動で消える。登録した単語も消える |
| ダウンロード | 中身すべて | このアプリが入っているフォルダーは残す。OneDrive 同期なら手を付けない |
| デスクトップ | ショートカット以外 | 同上 |
| ごみ箱 | 空にする | |
| Edge・Chrome | お気に入り、履歴（よく見るページ・アドレス欄の候補・開いていたタブを含む）、Cookie とサイトのデータ、キャッシュ。`browserClearPasswords` が true（標準）なら保存されたパスワードと自動入力も | ブラウザーは強制的に閉じる（開いていたタブは失われる）。設定（Preferences）・拡張機能・ブラウザーの内部用プロファイルは残す |

アプリを閉じるとき、プロファイルの `cleanOnExit` が true（標準）なら「作業でできた履歴を削除します」と出る。
［履歴を削除して終了する（講習 PC 用。※開発用 PC では実行しないでください）］（初期の選択）で、確認なしに［履歴を消す］を行い、終わったら閉じる（管理者の確認は出る）。
［履歴を削除せずに終了する（開発 PC 用）］［キャンセル］も選べる。消せなかったものがあれば知らせてから閉じる。

## 時間のかかる準備

| トグル | やり方 | 初期値 |
|---|---|---|
| Office | `officeSetupUrl` の Office 展開ツールを入手（Microsoft の署名を確認）し、`setup.exe /configure profiles\office.xml` | `office` が `install` ならオン |
| Adobe CC | `adobeInstallerUrl` のインストーラーを入手（Adobe の署名を確認）し、`--silent` で実行 | `adobe` が `install` ならオン |
| 独自アプリ | `ownAppsManifest`（目録）の ZIP を入手し、SHA-256 を確認して `C:\Program Files\Org_Apps\<folder>` に展開。前の版は `<folder>_old` に残す | 目録が書かれていればオン |

- すでに入っているもの（Office・Adobe）や同じ版（独自アプリ）は何もしない。
- **Office・Adobe CC のトグルをオフにして実行すると、その PC に入っている場合はメーカーの削除ツールで完全に削除する。**
  トグルには柿色で「→ 削除」と出る。確認画面に「削除するもの（元に戻せません）」を別に書き、初期の選択は「いいえ」。削除が先、入れるのが後。
  - Office：マイクロソフトの Get Help コマンドライン ツール（`officeRemoveToolUrl`）を毎回入手し（作成から 90 日で使えなくなるため）、署名を確かめて
    `GetHelpCmd.exe -S OfficeScrubScenario -AcceptEula -OfficeVersion All`。Office のアプリが開いていると削除しない。
    （SaRA のコマンドライン版は 2026 年 3 月に廃止された）
  - Office の控え：Get Help のツールが使えない・消せないときは、Office 展開ツール（setup.exe）に `<Remove All="TRUE"/>` の構成を渡して削除する（手順 5〜7）。
  - Adobe：Adobe のアプリと裏で動くものを閉じ、Creative Cloud のアンインストーラー（`Utils\Creative Cloud Uninstaller.exe -u`）があれば実行、
    そのあと Creative Cloud Cleaner Tool（`adobeCleanerUrl`）を `--eulaAccepted=1 --removeAll=ALL` で実行。
  - どちらも、終わったあと PC の再起動が要る。
- 作業ごとの行：準備を始めると、一覧の上に作業ごとの行が並ぶ（作業は 1 つずつ順番に行う。同時に動かすと回線を取り合い、インストーラー同士がぶつかるため）。
  1 段目「名前・状態（待機／作業中／完了／そのまま／失敗／見送り）・手順（例：手順 3/4　インストール）・進み具合のバーと %」、
  2 段目「説明（大きさ・速さ・残り時間など）」。終わったあとも、次の準備か、手で点検するまで残す。
  手順：Office のインストール 4（展開ツールのダウンロード／電子署名の確認／インストール／確認）、Adobe のインストール 4、
  Office の削除 4、Adobe の削除 5、独自アプリ（目録の入手＋アプリの数）。
- 進み具合の表示：
  - アプリが自分でダウンロードするもの（展開ツール・削除ツール・独自アプリの ZIP）は「12.3 MB / 45.0 MB（27%）・3.2 MB/秒（26 Mbps）・あと約 10 秒」。
  - Office・Adobe のインストーラーは進み具合を外に出さないため、PC 全体の受信量と速さを測り、`officeExpectedMB`（3500）・`adobeExpectedMB`（600）を目安に残り時間を見積もる（ほかの通信も混ざる）。
  - 何番目の作業か（2/3）と、全体の経過時間も出す。結果には全体の所要時間を残す。
  - インストーラーが起動できないときはすぐに失敗にする。6 時間たっても終わらないときは待つのをやめる。
- 作業の場所：`C:\ProgramData\ManabitoClassSetup\work`。
- 記録：`準備_日時.tsv`・`_PowerShell.txt`・`_状況.txt`（画面が 1 秒ごとに読む）・`_取り消し.flag`（取り消したときにできる）。
- 指定時刻が今より前なら、翌日のその時刻。前回の時刻は settings.ini の `[deploy] time` に残る。

### 独自アプリの目録（apps.json）

`docs\apps.sample.json` を参照。プロファイルの `ownAppsManifest` に、https の URL か、共有フォルダーなどのパスを書く（相対パスなら profiles フォルダーから）。

```json
{ "apps": [ { "name": "まなびとPDFツール", "version": "0.7.0", "url": "https://…/ManabitoPdfTool_v0.7.0.zip",
              "sha256": "（64 文字）", "folder": "Manabito_Pdf_Tool", "exe": "ManabitoPdfTool.exe", "shortcut": true } ] }
```

- `sha256` は必須（確かめずに入れることはしない）。ZIP の SHA-256 は PowerShell の `Get-FileHash ファイル名` で出せる。
- ZIP の中がフォルダー 1 つだけなら、その中身を展開する。
- `shortcut: true` で、全員のデスクトップにショートカットを作る。
- 入れた版は `<folder>\.manabito_version` に書く。アプリが起動していると入れ替えない。

### 電子署名の確認

ダウンロードしたツール・インストーラーは、電子署名が「Valid」なら使う。「UnknownError」（証明書の期限切れ、失効の確認ができない など）のときは、
改ざん（HashMismatch）ではないこと・発行元の名前・信頼できる元までたどれることを確かめて使う。確認の結果は PowerShell の記録に残す。

### Adobe のインストールについて

個人向けの Creative Cloud のインストーラーは、`--silent` でもサインイン画面を出して待つことがある（Adobe コミュニティの報告）。
そのため画面は隠さずに動かし、入ったと分かったら 60 秒後にサインイン画面とインストーラーを閉じて先へ進む。
15 分たっても受信が 10 MB に届かず、入った様子もなければ、打ち切って「失敗」にし、次の作業へ進む。
完全に無人で入れるには、Adobe の管理コンソールで作るパッケージが要る。

### 未確認（実機で確かめる）

- `officeSetupUrl`（officecdn の setup.exe）と、`/configure` が終わるまで待つこと。
- `adobeInstallerUrl` の URL と、`--silent` で画面を出さずに入るか。URL が変わっていたらプロファイルを直す。
- `officeRemoveToolUrl`（aka.ms/SaRA_EnterpriseVersionFiles）で Get Help のツールが入手できるか。
- `adobeCleanerUrl` の URL、`--eulaAccepted=1`（Adobe の公式文書には無く、利用者の報告による）で画面を出さずに動くか、`-u` のアンインストーラー。

## Program Files に入れる

Program Files 以外（ダウンロードなど）から起動すると、`C:\Program Files\Org_Apps\ManabitoClassSetup` に入れて、そこから起動し直す（管理者の確認が 1 回）。

- EXE と scripts は入れ替える（scripts は一度消してから写す）。profiles は上書きし、中身が違っていたものは `profiles_backup` に控えを残す。その PC で足したプロファイルは残す。
- デスクトップ（全員）とスタートメニュー（全員）に「まなびと講習準備」のショートカットを作る。
- 入っている版のほうが新しければ、入れ替えずにそちらを起動する。
- **元の場所を消すのは、ダウンロード・デスクトップ・ドキュメントの中から起動したときだけ**（共有フォルダー・USB・開発用のビルド先は消さない）。
  - 展開したフォルダーの名前が「ManabitoClassSetup」で始まれば、そのフォルダーごと（同じ名前の zip も）消す。
  - それ以外のフォルダーなら、このアプリのファイル（EXE・scripts・profiles）だけ消す。
  - 消すのは Program Files 側のアプリで、元のアプリが終わってから。ごみ箱には入れない。
- ［この場所のまま使う］を選ぶか、`--portable` を付けて起動すると、入れずにその場所で動く（開発・試験用）。
- 開いているアプリがあると入れ替えられないので、「閉じてから、もう一度起動してください」と出す。

## Office の LAN 配布（v0.14.0）

- 配布サーバーの窓の［Office のインストール元を用意する］：展開ツールの `/download` で、インストール元（約 3.4 GB）を
  `%LOCALAPPDATA%\ManabitoClassSetup\OfficeSource` に一度だけダウンロードする（scripts\office_source.ps1、管理者は要らない）。
  用意できると、窓を開いている間、自動で配る（合言葉なし）。
- 受け取る側（時間のかかる準備・まとめて実行の Office のインストール）：LAN を 3 秒探し、インストール元を持った配布サーバーがあれば
  受け取って（`%ProgramData%\ManabitoClassSetup\work\office_source`）手元からインストールし、終わったら消す。無ければインターネットから。
  `officeLanSource`（標準 true）で切り替え。
- 守り：setup.exe は毎回マイクロソフトから入手して署名を確かめる。LAN から受け取るのは `Office\` の下だけで、
  「..」やドライブ名を含む名前が 1 つでもあれば LAN からの受け取りをやめてインターネットから入れる。.cab はすべてマイクロソフトの署名を確かめる。
- 夜間の時間指定で使うときは、その時刻に配布サーバーの PC でアプリと配布の窓を開いておく。
- **Creative Cloud デスクトップアプリ（ランチャー）**も同じ形で配れる（v0.15.0）。配布サーバーの［Creative Cloud（ランチャー）を用意する］で、
  Adobe の公式ページ「Download Creative Cloud desktop app using direct links」を読み、Windows（64 ビット）用のいちばん新しい ZIP を入手する
  （scripts\adobe_source.ps1。https の *.adobe.com の ACCCx…zip だけ使う。`adobeOfflineZipUrl` で直接指定も可）。
  受け取る側は ZIP を展開し、中の .exe と .dll がすべて Adobe の署名つきかを確かめてから Set-up.exe を動かす。`adobeLanSource`（標準 true）。
- Photoshop などのアプリ本体は、受講生がサインインしたあと各 PC が Adobe から直接ダウンロードする（個人版のため配れない）。

## 前の受講生のサインインを消す（v0.14.0）

［履歴を消す］（閉じるときの削除・まとめて実行も）と点検の対象：

| 対象 | 消し方 |
|---|---|
| Edge・Chrome | User Data ごと消して、初めて開いたときの状態に（サインイン・プロファイル・拡張機能・パスワードも）。`browserFullReset`（標準 true） |
| Windows に追加されたアカウント | Office などがサインインすると「メールとアカウント」に残る。Windows の仕組み（WAM）でサインアウト |
| Office | サインインの記録（Identity・IdentityCache・OneAuth）。`officeRemoveLicenses`（標準 true）ならライセンスも |
| Adobe Creative Cloud | サインインの記録（%LOCALAPPDATA%\Adobe\OOBE\opm.db）。Creative Cloud を閉じてから |
| 保存されたログイン情報 | 資格情報マネージャーの「汎用資格情報」。ネットワーク共有・プリンター用（Windows 資格情報）は残す。残したいものは `keepCredentials` に語を書く |
| ほかのアプリ・ブラウザー | Firefox・Brave・Opera・Vivaldi・Zoom・LINE・Discord・Slack・Teams・ChatGPT・Claude・Google ドライブ・Dropbox・iCloud・WhatsApp・Spotify・Notion のデータ（ログインと設定）。`signOutApps`（標準 true） |
| この利用者が入れたアプリ | 点検で知らせるだけ（管理者なしで入れられたもの） |

- ブラウザーで Google・Apple・AI ツールなどにログインしていた分は、ブラウザーの初期化で消える。
- **開発用の PC では［履歴を消す］を実行しない**（git などのログインも消える）。
- 講習の間で PC を完全に基準の状態へ戻すには、まなびとバックアップでの復元が確実。

## 授業ファイルの配布（LAN 内）

**配布する側**：右上の［配布サーバーになる］→ 配布用の窓に、ファイル・フォルダー・ドライブ（USB メモリなど）をドロップする。
窓に 4 桁の合言葉が出る（受講生に見せる）。受け取った PC の一覧と進み具合が出る。窓を閉じると配布をやめる。
ドライブはボリューム名（無ければ「E ドライブ」）のフォルダーとして配る。Windows 用の隠しファイル（System Volume Information など）は配らない。
大きいとき（2 GB 超、5,000 個超）は確認する。配布中に USB メモリを抜くと、そのファイルは受け取れない。

**受け取る側**：アプリが開いている間、5 秒ごとに LAN を探す。見つかったら「LAN 内に配布サーバーがあります。ファイルを取得しますか？」→
合言葉 → 受け取るフォルダー（初期値：ドキュメント\授業ファイル。前回の場所を覚える）→ 進み具合の窓。同じ名前のファイルは上書き。
受け取ったら、そのフォルダーを開く。作業中は案内せず、終わってから案内する。

- 通信：UDP 47811（見つける）・TCP 47812（一覧と中身）。プロトコルは src\share.h の先頭に書いてある。
- 合言葉：同じ PC から 5 回続けて間違えると 1 分間受け付けない。
- 受け取る側の守り：届いたファイル名に「..」・ドライブ名・先頭の「\」などがあれば受け取らない（受け取るフォルダーの外に書かせない）。tests\test_safe_relpath.py で確かめられる。
- ファイアウォール：Program Files に入れるとき、このアプリの受け口を登録する（プライベート・ドメインのネットワークだけ）。
  ［配布サーバーになる］を押したとき、ネットワークが「パブリック」なら「プライベートに切り替えて始める／このまま始める」を聞く
  （scripts\network_private.ps1、管理者。今つながっているパブリックのものだけ。ドメインには手を付けない）。配布用の窓にも［プライベートに切り替える］。
  受け取る側は、自分から呼びかけてつなぎに行くので、パブリックのままでよい。
- ［履歴を消す］と点検に「受け取った授業ファイル」を追加。受け取る場所の中身を消す（ドライブの直下や利用者のフォルダーそのものなら消さない）。

## このアプリ自身の更新

独自アプリの目録（プロファイルの `ownAppsManifest` に書いた apps.json）に、このアプリ自身（`exe` が `ManabitoClassSetup.exe`）も載せる。

- 起動したとき（Program Files に入っているときだけ）、目録を見に行き、今より新しい版があれば「30 秒後に自動で更新して起動し直します」と出す。［今回は更新しない］も選べる。
- 更新するとき：管理者の確認 1 回 → 新しい版を入手 → SHA-256 が目録と合うかを確かめる → このアプリを閉じる → Program Files の中身を入れ替える（新しい版の install.ps1） → 起動し直す。
  入れ替えがうまくいかなくても、起動し直す（前の版のまま）。結果は起動し直したあとに表示する。
- 時間のかかる準備の「独自アプリ」では、このアプリ自身は扱わない（起動中の自分は入れ替えられないため）。
- 目録や ZIP は、https の URL でも、共有フォルダーのパスでもよい。

### 新しい版を出す手順（開発用 PC）

使うアプリ：**PowerShell**

```
cd C:\dev\ManabitoClassSetup
Compress-Archive -Path build\Release\* -DestinationPath ManabitoClassSetup_v0.13.0.zip -Force
.\tools\make_manifest_entry.ps1 -Zip ManabitoClassSetup_v0.13.0.zip -Name まなびと講習準備 -Version 0.13.0 `
    -Url https://（置き場所）/ManabitoClassSetup_v0.13.0.zip -Folder ManabitoClassSetup -Exe ManabitoClassSetup.exe
```

ZIP を置き場所に上げ、出てきた 1 件分で apps.json の「まなびと講習準備」の行を入れ替える。

## GitHub からの更新（公開の保管庫 manabito-class-setup）

- 保管庫は **公開（Public）**。PC が鍵なしでダウンロードできるようにするため（淳さんの選択）。合言葉や鍵はソースに入れない。
- `.github/workflows/release.yml`：版の印（タグ `v0.14.0` など）を送ると、Windows の上でビルド → EXE の版とタグを照合 → ZIP を Releases に置く →
  `release/apps.json` の「まなびと講習準備」の行（版・URL・SHA-256）を書き換えて main に保存。
- 各 PC は、プロファイルの `ownAppsManifest`（標準・WEB 講座のプロファイルに設定済み）に書いた `https://raw.githubusercontent.com/mucjun/manabito-class-setup/main/release/apps.json` を起動時に読み、
  新しい版があれば更新する（「このアプリ自身の更新」を参照）。
- **大事**：この保管庫に版を置ける人は、全 PC に管理者の権限でプログラムを入れられることになる。GitHub のアカウントには必ず 2 段階認証を設定する。

### はじめに一度だけ

1. GitHub で新しい保管庫 `manabito-class-setup` を **Public** で作る（README などは作らない）。
2. 使うアプリ：**コマンドプロンプト**

```
cd /d C:\dev\ManabitoClassSetup
git init
git add .
git commit -m "まなびと講習準備 v0.13.1"
git branch -M main
git remote add origin https://github.com/mucjun/manabito-class-setup.git
git push -u origin main
```

### 新しい版を出すたびに

1. 使うアプリ：**PowerShell**

```
cd C:\dev\ManabitoClassSetup
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\tools\bump_version.ps1 0.14.0
```

2. `docs\CHANGES_v0.14.0.md` に変更点を書く。
3. 使うアプリ：**コマンドプロンプト**

```
cd /d C:\dev\ManabitoClassSetup
git add .
git commit -m "v0.14.0"
git push
git tag v0.14.0
git push origin v0.14.0
```

4. GitHub の「Actions」で release が緑になれば完了。数分後から、各 PC は起動時に更新の案内を出す。
   赤（失敗）のときは、目録は書き換わらないので、PC には何も起きない。

## 二重に動かさない仕組み

- アプリは 2 つ同時に起動しない（名前付きミューテックス `Local\ManabitoClassSetup_Instance`）。すでに開いていれば、その窓を前に出す。
- そろえる・履歴を消す・時間のかかる準備は、同時に 2 つ走らない（`Global\ManabitoClassSetupWork`）。
  そろえる・履歴を消すは、別の作業が動いていれば止まる。時間のかかる準備は、開始時刻になってから順番を取り、最大 30 分待つ。
- v0.9.0 以前の版にはこの仕組みが無い。古い版のフォルダーは、アプリを閉じてから消しておく。

## 設定ファイル

`%LOCALAPPDATA%\ManabitoClassSetup\settings.ini`

- `[startup] sacPromptDismissed=1`：起動時のスマート アプリ コントロールの案内を出さない（0 にするか行を消すと、また出る）。

## 記録（logs）

1 回の点検で 3 つのファイルができます。「履歴を消す」では `履歴を消す_日時.tsv` と `_PowerShell.txt`、「そろえる」では `そろえる_日時.tsv`（結果）と `そろえる_日時_PowerShell.txt`（作業の記録）ができます。

| ファイル | 中身 |
|---|---|
| `点検_日時.tsv` | 一覧の元データ（UTF-8） |
| `点検_日時_詳細.txt` | 各項目の生の値、IME・メモ帳のファイル一覧、`powercfg /a` の出力 |
| `点検_日時_PowerShell.txt` | PowerShell のエラー出力（普段は空か警告だけ） |

## 点検する項目

| 区分 | 項目 | 講習用の状態 |
|---|---|---|
| 電源と画面 | 自動スリープ／画面の自動消灯／自動の休止状態 | しない |
| | ふたを閉じたとき（ノート PC） | 電源接続時は何もしない |
| | 電源と待機の方式 | 情報（モダンスタンバイかどうか） |
| | 画面の解像度と拡大率 | プロファイルの値（標準 1600×900・100%） |
| 見た目と通知 | デスクトップの背景 | 無地（プロファイルの色） |
| | ファイルの拡張子／ウィジェット／タスクバーの検索 | プロファイルの値 |
| | 通知／OneDrive の自動起動 | 出さない／起動しない |
| 音 | 音量 | 消音（または 0%） |
| 安全と更新 | Windows の種類 | 情報 |
| | スマート アプリ コントロール | オフ |
| | Windows Update | プロファイルの値（今は「確認のみ」） |
| 前の受講生の痕跡 | 最近使ったファイル／アドレス欄・実行の履歴／Office の最近使ったファイル／メモ帳のタブ／ダウンロード／デスクトップのファイル／ごみ箱 | 0 件・空 |
| | IME の学習 | 情報（置き場所の洗い出し） |
| アプリ | Microsoft 365／Adobe CC デスクトップアプリ／独自アプリ | プロファイルの値 |
| この PC | 既定のプリンター | 情報 |

## プロファイル（profiles\*.json）

BOM 付き UTF-8。主な値：

- `name`：画面に出す名前
- `wallpaperColor`：背景色（`#RRGGBB`、標準は `#1F3A5F`）
- `showFileExtensions`：`true`／`false`
- `taskbarWidgets`：`hide`／`show`／`check-only`
- `taskbarSearch`：`hide`／`icon`／`box`／`label`／`check-only`
- `windowsUpdate`：`stop`／`run`／`check-only`（標準は `stop`）
- `screen`：`{ "width": 1600, "height": 900, "scalePercent": 100 }`
- `office`・`adobe`：`install`／`skip`
- `ownApps`：独自アプリの `name`・`folder`・`exe` の並び

## ビルド

使うアプリ：**x64 Native Tools Command Prompt for VS**（PowerShell では動きません）

```
cd /d C:\dev\ManabitoClassSetup
BUILD_WINDOWS.cmd
```

できるもの：`build\Release\ManabitoClassSetup.exe`（`scripts` と `profiles` も同じ場所にコピーされます）。
CMake は使っていません（cl.exe を直接呼ぶ）ので、ソースが増えても `build` を消す必要はありません。

## 作りのメモ

- 点検：EXE は `powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts\inspect.ps1 ...` を窓なしで起動し、終わるのを待って TSV を読む。3 分で打ち切り。
- そろえる：`ShellExecuteEx`（runas）で `apply.ps1` を管理者として起動し、終わるのを待つ（5 分まで）。標準出力は受け取れないので、作業の記録は `Start-Transcript` で書く。
- `modules\set.ps1` の各関数は、先に点検用の `Test-*` で今の状態を見て、そろっていれば何もしない（何度押しても同じ結果になる）。
- 部品（modules）が `$script:` に置く変数は、呼び出す側のスクリプトの引数と同じ名前にしない（同じ場所に入り、読み込んだときに引数が消える）。`tests\check_scope.ps1` で確かめられる。
- 大きな数（受信量など、2 GB を超えるもの）を扱う計算では、`[Math]::Max(0, …)` のように整数の 0 を書かない。Windows PowerShell 5.1 は整数（Int32）の計算を選んで、2 GB を超えるとエラーになる（PowerShell 7 では起きない）。`[double]0` と書く。
- PowerShell の部品は **BOM 付き UTF-8** で保存する（BOM が無いと Windows PowerShell 5.1 が日本語を Shift_JIS として読む）。
- 結果は標準出力ではなくファイルで受け取る（文字化けを避けるため）。
- 画面の解像度と拡大率は EXE が測って `-Screen 幅x高さxDPI` で渡す（PowerShell からは拡大率を正しく測りにくいため）。
- `powercfg /q` は表示の言語に左右されないよう、`0x` で始まる値の最後の 2 つ（AC・DC）だけを読む。
- 開発用：`ManabitoClassSetup.exe --sample 結果.tsv` で、点検せずに TSV を表示する。

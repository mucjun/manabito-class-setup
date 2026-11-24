# v0.20.8（2026-11-24）WEB系に Figma と Local を加える

淳さんの要望：WEB系講座で Figma（デスクトップ アプリ）と Local（localwp.com）を入れる。ダウンロード時の登録が問題になるか調べる。

## 調べた結果

- どちらも winget に登録がある（Figma.Figma・Flywheel.Local）。インストーラーは公式の配布元から直接（desktop.figma.com・cdn.localwp.com）で、登録は要らない。
  ダウンロードのページの登録フォームは、ブラウザーで手で落とすときだけのもの。1 台が落として配る方法は要らない。
- Figma：PC 全体（--scope machine）では MSI 版が入る。MSI 版は自動で更新されない（Figma のヘルプ）。使うときは Figma のアカウントでサインインする。
- Local：同じインストーラーで、PC 全体のときは /ALLUSERS（winget の登録情報）。サイトを作るとき hosts を書き換えるので管理者の確認が出る。

## 変えたこと

- scripts\extra_apps.txt に figma（目印 Figma*）・localwp（目印「Local [0-9]*」。名前に Local を含むほかのアプリと取り違えない）。
- profiles\web.json の extraApps に figma・localwp。
- ［履歴を消す］の「ほかのアプリのログイン」に Figma（AppData\Roaming\Figma）・Local（AppData\Roaming\Local。サイトの一覧を含む）。
- マニュアル（docs\まなびと講習準備_マニュアル.docx）の 7 章の表と注意。

## 実機で確かめること

- WEB系で［まとめて実行］→ Figma と Local が入り、スタートメニューに出るか（全員のアカウントで使えるか）。
- 点検の「この講座で入れるアプリ」で、入っていると判定されるか。
- ［履歴を消す］で Figma のサインインが消えるか。

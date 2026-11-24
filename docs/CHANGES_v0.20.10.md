# v0.20.10（2026-11-24）講座を切り替えてもアプリは残す・知らせる・アプリの設定を消す

淳さんの決定（A）：WEB系 → 事務系などに切り替えても、前の講座で入れたアプリは残す。サインイン情報と、前の受講生が変えたアプリの設定は消す。

- 前回［設定をそろえる］［まとめて実行］をしたプロファイルを %LOCALAPPDATA%\ManabitoClassSetup\last_profile.txt に覚える。
  違うプロファイルで行うとき「プロファイルを切り替えた場合、別のプロファイルで準備したアプリは残ります。サインイン情報や設定は消えます。」と出す（OK で続ける）。
- Office・Adobe CC のスイッチ：すでに入っているものは、プロファイルで「入れない」でもオンにする（＝消さずに残す）。
  以前は、事務系に切り替えると Adobe CC が削除の対象になり、上の知らせと動きが合わなかった。消すときは手でオフにする。
- ［履歴を消す］と点検に「アプリの設定（前の受講生が変えたもの）」：Word の Normal.dotm・NormalEmail.dotm、Office の *.officeUI（リボンとクイック アクセス ツール バー）、
  Excel の XLSTART、Adobe CC のアプリの設定（AppData\Roaming\Adobe の中。Acrobat のフォルダーは残す）、Acrobat Reader の最近使ったファイル（cRecentFiles・cRecentFolders）、
  Paint.NET の設定（HKCU\Software\paint.net）。アプリが開いているときは、そのアプリの分は消さずに知らせる。Figma・Local・VS Code は v0.20.8 から設定ごと消している。
- マニュアル：4 章に「講座を切り替えるとき」、5 章の「消すもの」にアプリの設定。
- 確かめたこと（この検証環境）：点検（前：Word・Excel・Office のリボン・Adobe CC → 後：なし）、Acrobat のフォルダーは残る。MinGW のビルド。

## 実機で確かめること

- WEB系でそろえたあと事務系で［まとめて実行］→ 知らせが出る、確認の画面に「Adobe CC：削除」が出ない、Adobe CC・Figma・Local が残る。
- Word で標準のフォントを変えて保存 →［履歴を消す］→ 新しい文書が初めの設定に戻る。

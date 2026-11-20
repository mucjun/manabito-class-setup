# v0.6.0（2026-11-20）Office・Adobe の削除

- 淳さんの要望：トグルを外して実行すると、メーカー提供の削除ツールで完全に削除する。
- 削除するのは、トグルがオフで、その PC に実際に入っているときだけ（画面が起動時・点検後・準備後に調べる）。
  - トグルに柿色で「→ 削除」。確認画面に「削除するもの（元に戻せません）」を別に書き、初期の選択を「いいえ」に。
  - deploy.ps1 の -Office・-Adobe は 0=何もしない 1=入れる 2=削除。削除を先に行う。
- Office：Get Help コマンドライン ツール（GetHelpCmd.exe、OfficeScrubScenario、-OfficeVersion All）。
  SaRA のコマンドライン版は 2026-03-10 に Windows から取り除かれたため、その後継を使う。作成から 90 日で使えなくなるので毎回入手。
  Microsoft の署名を確認。Office のアプリが開いていれば削除しない。終了コード 8・9・10・66・67 を日本語で表示。
- Adobe：Adobe のプロセスを閉じ、Creative Cloud Uninstaller（-u）→ Creative Cloud Cleaner Tool（--eulaAccepted=1 --removeAll=ALL）。Adobe の署名を確認。
- プロファイルに officeRemoveToolUrl・adobeCleanerUrl を追加。

## 未確認（実機で）

- 両方の削除ツールの URL と、画面を出さずに最後まで動くか。とくに Adobe の --eulaAccepted=1 は公式文書に無い。
- 2 段目の幅（トグルを広げたので、最小幅を 1080 に）。

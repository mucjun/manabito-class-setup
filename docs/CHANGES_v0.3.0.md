# v0.3.0（2026-11-20）起動時のスマート アプリ コントロールの案内

- 起動時に、スマート アプリ コントロールが「評価中」または「オン」なら案内を出す（淳さんの要望）。
  - 「オフにする（おすすめ）」／「今はオフにしない（独自アプリが起動しなくなる可能性があります）」。
  - 「次回から、この案内を表示しない」（settings.ini の sacPromptDismissed）。
  - 注意書き：一度オフにすると版によっては初期化するまで戻せない。Defender と SmartScreen はオフ後も働く。
- オフにする処理は scripts\sac_off.ps1（管理者として起動）。VerifiedAndReputablePolicyState を 0 にし、CiTool があれば --refresh。
  Microsoft の公式な手順ではないため、効かなかったときは「Windows セキュリティ」（アプリとブラウザーの制御）を開いて手で切ってもらう。
- 記録：logs\スマートアプリコントロール_日時.tsv／_PowerShell.txt。設定ファイルは %LOCALAPPDATA%\ManabitoClassSetup\settings.ini。

## 未確認

- 実機で、レジストリの書き換えと CiTool で本当にオフになるか（再起動の要否も含めて）。
- 「Windows セキュリティ」の画面がアプリとブラウザーの制御で開くか。
- v0.2.0 の「そろえる」、MSVC でのビルド。

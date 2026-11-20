# v0.1.0（2026-11-20）点検版

- 最初の版。PC の状態を点検して一覧にする（設定は変えない）。
- 画面：Win32（C++20）。紙色の地・角丸ボタン、強調色は藍（#2D5B8A）。
- 点検：PowerShell の部品 8 本（common, power, appearance, sound, security, traces, apps, system）と、まとめ役の inspect.ps1。
- プロファイル：standard.json（標準）、web.json（WEB 講座の例。Adobe を入れる）。値は仮。
- 記録：%LOCALAPPDATA%\ManabitoClassSetup\logs に TSV・詳細・PowerShell 出力。

## 未確認

- MSVC でのビルド、実機での画面と各項目の値。
- Home・Enterprise での表示、ノート PC（ふた・電池・モダンスタンバイ）。
- IME の学習ファイルの置き場所（詳細の記録で洗い出す）。

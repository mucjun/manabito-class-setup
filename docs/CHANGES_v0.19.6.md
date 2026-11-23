# v0.19.6（2026-11-23）ロック画面・サインイン画面の写真を出さない

淳さんの要望：起動したときのログイン画面に写真が出る、ログイン後のデスクトップに「この写真について」が出る。これらを消し、ログイン画面は単色に。

- ［設定をそろえる］と点検に「ロック画面・サインイン画面の写真」（scripts\modules\quiet.ps1 の lockscreen。プロファイル lockScreenPhoto、標準 hide）。
  1. サインイン画面を単色に：HKLM\SOFTWARE\Policies\Microsoft\Windows\System の DisableLogonBackgroundImage=1（設定アプリのスイッチと同じ。Pro でも効く）
  2. ロック画面を出さない：HKLM\SOFTWARE\Policies\Microsoft\Windows\Personalization の NoLockScreen=1（Pro では効かないことがある）
  3. ロック画面のスポットライトを止める：HKCU の ContentDeliveryManager の RotatingLockScreenEnabled=0（2 が効かなくても、写真が毎日替わらない）
  4. デスクトップの「この写真について」を隠す：HKCU の HideDesktopIcons\NewStartPanel の {2cc5ca98-6485-489a-920e-b3e88a6ccce3}=1
  5. デスクトップのスポットライトを止める：HKCU の Policies\Microsoft\Windows\CloudContent の DisableSpotlightCollectionOnDesktop=1
- 書けない値は失敗にせず、手で変える方法を案内する（ほかの「気が散るもの」と同じ）。
- 確かめたこと（この検証環境）：何も無い→「出す（5 か所）」、そろえた→ OK、1 か所欠け→「出す（1 か所）」。

## 加えたもの（作業の手順）

- tools\release.ps1：新しい版を GitHub に出す手順（取り込み・zip の上書き・目録を戻す・コミット・送る・印）を 1 行で行う。
  GitHub の代わりの保管庫で試した：Actions の目録の書き換えを取り込んでから出せる、目録は GitHub のものを残す、
  同じ版の印がある・コミットしていない変更がある・zip が無いときは止まり、GitHub には何も送らない。
  自分でのレビューと試験で直したこと：git の出力を判定に混ぜていた、関数の名前を Git にして git.exe の代わりに自分を呼び続けた。

## 実機で確かめること（再起動のあと）

- サインイン画面が単色になるか。
- ロック画面（時計の画面）が出なくなるか（Pro で NoLockScreen が効くか）。出る場合、写真が毎日替わらなくなるか。
- デスクトップの「この写真について」が消えるか。

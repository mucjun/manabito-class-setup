# v0.5.1（2026-11-20）不具合の修正

淳さんの動作確認（v0.4.0、ノート PC）で見つかったもの。

- ［履歴を消す］が「このアプリのフォルダーが分からない」と出て、何も消さずに止まっていた。
  原因：clean.ps1 の引数 -ProtectPath と、部品 modules\clean.ps1 の $script:ProtectPath が同じ名前で、
  部品を読み込んだときに引数の値を空にしていた。部品側の名前を $script:GuardPath に変えた。
- 同じ種類の不具合が v0.5.0 の「時間のかかる準備」にもあった（-StatusFile・-CancelFile）。
  状況が画面に出ない・取り消しが効かない状態だった。部品側を $script:DeployStatusFile・$script:DeployCancelFile に変えた。
- 再発を防ぐため tests\check_scope.ps1 を追加（直す前の版で 3 か所を検出、直した版で 0 件）。
- OneDrive の自動起動が「そろえる」のあとも「起動する」のままだった。
  スタートアップアプリの「無効」の印（StartupApproved）が、書いたそばから消されていたため（点検で「（なし）」）。
  自動起動の登録（Run の OneDrive）そのものを外す方法に変更。元の値は HKCU\Software\ManabitoClassSetup の OneDriveRun に控える。

## 未確認

- 実機での［履歴を消す］（IME の再起動時削除を含む）。
- 実機での OneDrive の新しいやり方（OneDrive が登録し直さないか）。

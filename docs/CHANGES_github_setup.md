# GitHub からの更新の準備（2026-11-20、v0.13.1 の時点）

淳さんの選択：A（ソースごと公開の保管庫）。

- .github/workflows/release.yml：タグ v* → ビルド（vswhere で Visual Studio を探し vcvars64 → BUILD_WINDOWS.cmd）→ EXE の版とタグの照合 →
  ZIP を Releases → release/apps.json の自分の行を書き換えて main に push。
- release/apps.json：最初は版 0.0.0（どの PC も更新を始めない）。
- tools/bump_version.ps1：版を src\main.cpp・res\app.rc・res\app.manifest の 3 か所でそろえる（0.14 のような誤りは受け付けない）。
- .gitignore：build/・dist-mingw/・*.zip。
- 検証：bump_version で 3 か所がそろうこと、目録の書き換えの処理と、それをアプリの確認（selfupdate_check.ps1）が「新しい版あり」と読むこと。

## アカウント名

- GitHub のアカウント名は mucjun（淳さんのログイン画面で確認。manabit-print-tauri と同じアカウント）。
- 標準・WEB 講座のプロファイルの ownAppsManifest に https://raw.githubusercontent.com/mucjun/manabito-class-setup/main/release/apps.json を設定。
  保管庫ができるまでは、確認が「見つからない」になるだけで、何も起きない。

## 起動時の更新の確認を速く

- 目録が見つからない（404 など）ときも、30 秒・60 秒と待って 3 回取り直していたため、確認が長引いていた（保管庫を作る前の試験で発見）。
  起動時の確認（selfupdate_check.ps1）では 1 回で諦める（$script:DownloadTries = 1）。時間のかかる準備は今までどおり 3 回。

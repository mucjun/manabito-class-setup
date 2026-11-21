# まなびと講習準備 ― Windows Update に、まだ入っていない更新があるかを確かめる（管理者は要らない。1〜数分かかる）
#
# 画面のアプリが起動時に裏で呼ぶ:
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File wu_check.ps1 -OutFile <wu_status.txt>
#
# 結果：1 行目 done|日時|件数|うちセキュリティ ／ error|日時|理由、2 行目から更新の名前（最大 10 件）
# 数えるもの：ソフトウェアの更新のうち、セキュリティ更新・重要な更新・更新プログラム・更新のロールアップ。
# 数えないもの：機能更新（Upgrades）・ドライバー・定義ファイル（Defender は別に点検）・プレビュー。

param([Parameter(Mandatory = $true)][string]$OutFile)

$utf8 = New-Object System.Text.UTF8Encoding $true
$now = (Get-Date).ToString('yyyy/MM/dd HH:mm')
try {
    $session = New-Object -ComObject Microsoft.Update.Session
    $searcher = $session.CreateUpdateSearcher()
    $searcher.Online = $true
    $result = $searcher.Search("IsInstalled=0 and IsHidden=0 and Type='Software'")
    $count = 0; $sec = 0
    $names = New-Object System.Collections.Generic.List[string]
    foreach ($u in $result.Updates) {
        $classes = @($u.Categories | Where-Object { $_.Type -eq 'UpdateClassification' } | ForEach-Object { $_.Name })
        if ($classes -match 'Upgrades|Definition|定義|アップグレード') { continue }
        if ($u.Title -match 'Preview|プレビュー') { continue }
        $count++
        if ($classes -match 'Security|Critical|セキュリティ|重要') { $sec++ }
        if ($names.Count -lt 10) { $names.Add($u.Title) }
    }
    $lines = @(('done|{0}|{1}|{2}' -f $now, $count, $sec)) + $names.ToArray()
    [System.IO.File]::WriteAllLines($OutFile, [string[]]$lines, $utf8)
} catch {
    [System.IO.File]::WriteAllLines($OutFile, [string[]]@(('error|{0}|{1}' -f $now, ($_.Exception.Message -replace '[|\r\n]+', ' '))), $utf8)
}
exit 0

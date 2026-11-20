# まなびと講習準備 ― 今つながっているネットワークのうち「パブリック」のものを「プライベート」にする（管理者として動く）
#
# 配布サーバーになるときに、画面のアプリが呼ぶ:
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File network_private.ps1 -OutFile <結果.txt>
# 結果.txt：1 行目 OK か NG、2 行目から説明。ドメインのネットワークには手を付けない。

param([Parameter(Mandatory = $true)][string]$OutFile)

$lines = New-Object System.Collections.Generic.List[string]
$ok = $true
try {
    $profiles = @(Get-NetConnectionProfile -ErrorAction Stop)
    foreach ($p in $profiles) {
        if ($p.NetworkCategory -ne 'Public') { $lines.Add(('{0}（{1}）：{2} のまま' -f $p.Name, $p.InterfaceAlias, $p.NetworkCategory)); continue }
        try {
            Set-NetConnectionProfile -InterfaceIndex $p.InterfaceIndex -NetworkCategory Private -ErrorAction Stop
            $lines.Add(('{0}（{1}）：パブリック → プライベート' -f $p.Name, $p.InterfaceAlias))
        } catch {
            $ok = $false
            $lines.Add(('{0}（{1}）：切り替えられませんでした（{2}）' -f $p.Name, $p.InterfaceAlias, $_.Exception.Message))
        }
    }
} catch {
    $ok = $false
    $lines.Add('ネットワークの一覧を読めませんでした：' + $_.Exception.Message)
}
$head = 'NG'; if ($ok) { $head = 'OK' }
[System.IO.File]::WriteAllLines($OutFile, [string[]](@($head) + $lines.ToArray()), (New-Object System.Text.UTF8Encoding $true))
exit 0

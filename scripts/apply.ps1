# まなびと講習準備 ― 講習用に設定をそろえる
#
# 画面のアプリから「管理者として」呼ばれる:
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File apply.ps1
#       -ProfilePath <プロファイル.json> -OutFile <結果.tsv> -LogFile <記録.txt> -ExpectedUser <利用者名>
#
# 元に戻せる設定だけを変える。履歴・ファイルは消さない。
# 結果.tsv の列は点検と同じ（区分, ID, 項目, 結果, 変える前, 変えた後, 説明）。結果は DONE/SAME/SKIP/FAIL。

param(
    [string]$ProfilePath = '',
    [Parameter(Mandatory = $true)][string]$OutFile,
    [string]$LogFile = '',
    [string]$ExpectedUser = ''
)

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($LogFile) { try { Start-Transcript -LiteralPath $LogFile -Force | Out-Null } catch { } }

foreach ($m in @('common', 'power', 'appearance', 'sound', 'security', 'set')) {
    . (Join-Path $here ('modules\' + $m + '.ps1'))
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("#区分`tID`t項目`t結果`t変える前`t変えた後`t説明")

function Clean-Field {
    param($Value)
    if ($null -eq $Value) { return '' }
    return ([string]$Value) -replace "[`t`r`n]+", ' '
}

function Write-Result {
    $utf8 = New-Object System.Text.UTF8Encoding $true
    $tmp = $OutFile + '.part'
    [System.IO.File]::WriteAllLines($tmp, [string[]]$lines.ToArray(), $utf8)
    Move-Item -LiteralPath $tmp -Destination $OutFile -Force
}

function Stop-WithError {
    param([string]$Id, [string]$Message)
    $lines.Add(("そろえる`t{0}`t{1}`tFAIL`t`t`t" -f $Id, 'そろえる前の確認') + (Clean-Field $Message))
    Write-Result
    if ($LogFile) { try { Stop-Transcript | Out-Null } catch { } }
    exit 2
}

# 別の利用者の資格情報で管理者になったときは、その人の設定を変えてしまうので中止する
if ($ExpectedUser -and ($env:USERNAME -ne $ExpectedUser)) {
    Stop-WithError 'user' ('画面を開いた利用者（{0}）と、管理者として動いている利用者（{1}）が違うため中止しました。講習で使う利用者自身が管理者の確認に「はい」で答えてください。' -f $ExpectedUser, $env:USERNAME)
}
if (-not (Enter-WorkLock 0)) {
    Stop-WithError 'busy' '別の作業（設定をそろえる・履歴を消す・時間のかかる準備）が動いているため中止しました。終わってからもう一度押してください。'
}
if (-not (Test-IsAdmin)) {
    Stop-WithError 'admin' '管理者として動いていないため中止しました。'
}

$prof = $null
if ($ProfilePath) {
    try {
        $prof = Get-Content -LiteralPath $ProfilePath -Raw -Encoding UTF8 -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    } catch {
        Stop-WithError 'profile' ('プロファイルを読めませんでした：' + $_.Exception.Message)
    }
}

$rows = New-Object System.Collections.Generic.List[object]
Invoke-ApplyAll $prof $rows
foreach ($r in $rows) {
    $fields = @('そろえる', $r.Id, $r.Name, $r.State, $r.Current, $r.Expected, $r.Detail) | ForEach-Object { Clean-Field $_ }
    $lines.Add(($fields -join "`t"))
}

Write-Result
if ($LogFile) { try { Stop-Transcript | Out-Null } catch { } }
exit 0

# まなびと講習準備 ― 前の受講生の履歴を消す
#
# 画面のアプリから「管理者として」呼ばれる:
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File clean.ps1
#       -ProfilePath <プロファイル.json> -OutFile <結果.tsv> -LogFile <記録.txt> -ExpectedUser <利用者名> -ProtectPath <アプリのフォルダー>
#
# 消したものは元に戻せない（ごみ箱にも入れない）。
# 結果.tsv の列は「そろえる」と同じ。結果は DONE/SAME/SKIP/FAIL。

param(
    [string]$ProfilePath = '',
    [Parameter(Mandatory = $true)][string]$OutFile,
    [string]$LogFile = '',
    [string]$ExpectedUser = '',
    [string]$ProtectPath = ''
)

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($LogFile) { try { Start-Transcript -LiteralPath $LogFile -Force | Out-Null } catch { } }

foreach ($m in @('common', 'set', 'clean')) {
    . (Join-Path $here ('modules\' + $m + '.ps1'))
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("#区分`tID`t項目`t結果`t消す前`t消した後`t説明")

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
    $lines.Add(("履歴を消す`t{0}`t{1}`tFAIL`t`t`t" -f $Id, '消す前の確認') + (Clean-Field $Message))
    Write-Result
    if ($LogFile) { try { Stop-Transcript | Out-Null } catch { } }
    exit 2
}

if ($ExpectedUser -and ($env:USERNAME -ne $ExpectedUser)) {
    Stop-WithError 'user' ('画面を開いた利用者（{0}）と、管理者として動いている利用者（{1}）が違うため中止しました。別の人の履歴を消さないためです。' -f $ExpectedUser, $env:USERNAME)
}
if (-not (Enter-WorkLock 0)) {
    Stop-WithError 'busy' '別の作業（設定をそろえる・履歴を消す・時間のかかる準備）が動いているため中止しました。終わってからもう一度押してください。'
}
if (-not (Test-IsAdmin)) {
    Stop-WithError 'admin' '管理者として動いていないため中止しました。'
}
# アプリの場所が分からないときも全体は止めない。
# 場所が関係するダウンロードとデスクトップだけ、消さずにエラーとして知らせる。
$script:GuardPath = ''
if ($ProtectPath) {
    try {
        $full = [System.IO.Path]::GetFullPath($ProtectPath)
        if (Test-Path -LiteralPath $full -PathType Container) { $script:GuardPath = $full }
    } catch { }
}
if ($script:GuardPath) {
    Write-Output ('残すフォルダー: ' + $script:GuardPath)
} else {
    Write-Output ('アプリの場所が分かりません（受け取った値: "' + $ProtectPath + '"）。ダウンロードとデスクトップは消しません。')
}

$prof = $null
if ($ProfilePath) {
    try { $prof = Get-Content -LiteralPath $ProfilePath -Raw -Encoding UTF8 -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop } catch { }
}

$rows = New-Object System.Collections.Generic.List[object]
Invoke-CleanAll $prof $rows
foreach ($r in $rows) {
    $fields = @('履歴を消す', $r.Id, $r.Name, $r.State, $r.Current, $r.Expected, $r.Detail) | ForEach-Object { Clean-Field $_ }
    $lines.Add(($fields -join "`t"))
}

Write-Result
if ($LogFile) { try { Stop-Transcript | Out-Null } catch { } }
exit 0

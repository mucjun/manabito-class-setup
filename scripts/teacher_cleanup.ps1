# まなびと講習準備 ― 先生の PC の講習後の片付け（v0.20.0）
#
# 先生用の窓の［講習後の片付け］から「管理者として」呼ばれる:
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File teacher_cleanup.ps1
#       -ProfilePath <プロファイル.json> -OutFile <結果.tsv> -LogFile <記録.txt> -ExpectedUser <利用者名> -ProtectPath <アプリのフォルダー>
# 結果.tsv の列は［履歴を消す］と同じ。

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

foreach ($m in @('common', 'set', 'clean', 'signout', 'teacher')) {
    . (Join-Path $here ('modules\' + $m + '.ps1'))
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("#区分`tID`t項目`t結果`t前`t後`t説明")

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
    $lines.Add(("先生の片付け`t{0}`t{1}`tFAIL`t`t`t" -f $Id, '始める前の確認') + (Clean-Field $Message))
    Write-Result
    if ($LogFile) { try { Stop-Transcript | Out-Null } catch { } }
    exit 2
}

function Add-Rows {
    param([string]$Group, $Rows)
    foreach ($r in @($Rows)) {
        if ($null -eq $r -or -not $r.PSObject.Properties['State']) { continue }
        $fields = @($Group, $r.Id, $r.Name, $r.State, $r.Current, $r.Expected, $r.Detail) | ForEach-Object { Clean-Field $_ }
        $lines.Add(($fields -join "`t"))
        Write-Host ('[{0}] {1} ／ {2}' -f $r.State, $r.Name, $r.Detail)
    }
}

if ($ExpectedUser -and ($env:USERNAME -ne $ExpectedUser)) {
    Stop-WithError 'user' ('画面を開いた利用者（{0}）と、管理者として動いている利用者（{1}）が違うため中止しました。別の人のファイルを動かさないためです。' -f $ExpectedUser, $env:USERNAME)
}
if (-not (Enter-WorkLock 0)) { Stop-WithError 'busy' '別の作業が動いているため中止しました。終わってからもう一度押してください。' }
if (-not (Test-IsAdmin)) { Stop-WithError 'admin' '管理者として動いていないため中止しました。' }
$script:GuardPath = ''
if ($ProtectPath) {
    try { $full = [System.IO.Path]::GetFullPath($ProtectPath); if (Test-Path -LiteralPath $full -PathType Container) { $script:GuardPath = $full } } catch { }
}
if (-not $script:GuardPath) { Stop-WithError 'guard' 'アプリの場所が分からないため、ファイルを動かさずに中止しました。' }

$prof = $null
if ($ProfilePath) {
    try { $prof = Get-Content -LiteralPath $ProfilePath -Raw -Encoding UTF8 -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop } catch { }
}

# 1. ファイルを保管のフォルダーへ移す（消さない）。履歴を消す前に行う（ブラウザーの初期化などでファイルが失われないように）
Add-Rows 'ファイルの片付け' (Move-TeacherFiles $prof)

# 2. 受講生の PC と同じ［履歴を消す］。ダウンロード・デスクトップ・授業ファイルは消さない（1 で移した。移せなかったものは残す）
$rows = New-Object System.Collections.Generic.List[object]
Invoke-CleanAll $prof $rows @('Clear-Downloads', 'Clear-DesktopFiles', 'Clear-ClassFiles')
Add-Rows 'サインアウトと履歴' $rows

# 3・4. 元に戻す
Add-Rows '元に戻す' @(
    (Set-DisplayClone $prof),
    (Set-DefaultSpeakers $prof),
    (Set-TeacherDefaultPrinter $prof),
    (Clear-ClipboardHistory $prof),
    (Clear-ForeignWifi $prof)
)

Write-Result
if ($LogFile) { try { Stop-Transcript | Out-Null } catch { } }
exit 0

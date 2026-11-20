# まなびと講習準備 ― まとめて実行（ワンクリック）
#
# 設定をそろえる → アプリの削除とインストール（トグルのとおり）→ 履歴を消す、を 1 回の管理者の確認で続けて行う。
# 履歴を消すのを最後にするのは、インストールでできたファイルや履歴もまとめて片付けるため。
#
# 画面のアプリから「管理者として」呼ばれる:
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File oneclick.ps1
#       -ProfilePath <プロファイル.json> -OutFile <結果.tsv> -LogFile <記録.txt> -StatusFile <状況.txt> -CancelFile <取り消し.flag>
#       -ExpectedUser <利用者名> -ProtectPath <アプリのフォルダー> -Office 0/1/2 -Adobe 0/1/2 -OwnApps 0/1
#
# 状況.txt は「時間のかかる準備」と同じ形（作業ごとの行）。結果.tsv の 1 列目で、そろえる／準備／履歴を消す を分ける。

param(
    [string]$ProfilePath = '',
    [Parameter(Mandatory = $true)][string]$OutFile,
    [string]$LogFile = '',
    [string]$StatusFile = '',
    [string]$CancelFile = '',
    [string]$ExpectedUser = '',
    [string]$ProtectPath = '',
    [int]$Office = 0,
    [int]$Adobe = 0,
    [int]$OwnApps = 0
)

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($LogFile) { try { Start-Transcript -LiteralPath $LogFile -Force | Out-Null } catch { } }

foreach ($m in @('common', 'power', 'appearance', 'sound', 'security', 'traces', 'set', 'clean', 'signout', 'deploy')) {
    . (Join-Path $here ('modules\' + $m + '.ps1'))
}
$script:DeployStatusFile = $StatusFile
$script:DeployCancelFile = $CancelFile

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("#区分`tID`t項目`t結果`t前`t後`t説明")

function Clean-Field {
    param($Value)
    if ($null -eq $Value) { return '' }
    return ([string]$Value) -replace "[`t`r`n]+", ' '
}

function Add-Rows {
    param([string]$Group, $Rows)
    foreach ($r in $Rows) {
        $fields = @($Group, $r.Id, $r.Name, $r.State, $r.Current, $r.Expected, $r.Detail) | ForEach-Object { Clean-Field $_ }
        $lines.Add(($fields -join "`t"))
    }
}

function Finish {
    param([int]$Code)
    Set-KeepAwake $false
    if ($script:DeployHead -like 'head|run|*') { $script:DeployHead = $script:DeployHead -replace '^head\|run\|', 'head|done|' }
    Save-DeployStatus
    $utf8 = New-Object System.Text.UTF8Encoding $true
    $tmp = $OutFile + '.part'
    [System.IO.File]::WriteAllLines($tmp, [string[]]$lines.ToArray(), $utf8)
    Move-Item -LiteralPath $tmp -Destination $OutFile -Force
    if ($LogFile) { try { Stop-Transcript | Out-Null } catch { } }
    exit $Code
}

function Stop-WithError {
    param([string]$Id, [string]$Message)
    $lines.Add(("まとめて`t{0}`t始める前の確認`tFAIL`t`t`t" -f $Id) + (Clean-Field $Message))
    Finish 2
}

# 作業ごとの行の結果（FAIL があれば失敗、DONE があれば完了、どれも SAME ならそのまま）
function Exit-ItemByRows {
    param($Rows, [string]$DoneText)
    $fails = @($Rows | Where-Object { $_.State -eq 'FAIL' })
    $dones = @($Rows | Where-Object { $_.State -eq 'DONE' -and $_.Id -ne 'explorer' })
    if ($fails.Count -gt 0) { Exit-DeployItem 'fail' ('{0} 件うまくいきませんでした（{1} など）' -f $fails.Count, $fails[0].Name) }
    elseif ($dones.Count -gt 0) { Exit-DeployItem 'done' ($DoneText -f $dones.Count) }
    else { Exit-DeployItem 'same' 'すでにそろっていたので、何もしませんでした' }
}

# ---- 始める前の確認
if ($ExpectedUser -and ($env:USERNAME -ne $ExpectedUser)) {
    Stop-WithError 'user' ('画面を開いた利用者（{0}）と、管理者として動いている利用者（{1}）が違うため中止しました。' -f $ExpectedUser, $env:USERNAME)
}
if (-not (Test-IsAdmin)) { Stop-WithError 'admin' '管理者として動いていないため中止しました。' }
if (-not (Enter-WorkLock 0)) {
    Stop-WithError 'busy' '別の作業（設定をそろえる・履歴を消す・時間のかかる準備）が動いているため中止しました。終わってからもう一度押してください。'
}
$prof = $null
$profileDir = Join-Path (Split-Path -Parent $here) 'profiles'
if ($ProfilePath) {
    try {
        $prof = Get-Content -LiteralPath $ProfilePath -Raw -Encoding UTF8 -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        $profileDir = Split-Path -Parent $ProfilePath
    } catch { Stop-WithError 'profile' ('プロファイルを読めませんでした：' + $_.Exception.Message) }
}
$script:GuardPath = ''
if ($ProtectPath) {
    try { $full = [System.IO.Path]::GetFullPath($ProtectPath); if (Test-Path -LiteralPath $full -PathType Container) { $script:GuardPath = $full } } catch { }
}

# ---- 作業の一覧（画面の行）：設定 → アプリ → 履歴
Register-DeployItem 'apply' '設定をそろえる' 1
$steps = Register-DeployPlan $Office $Adobe $OwnApps
Register-DeployItem 'clean' '前の受講生の履歴を消す' 1
Set-KeepAwake $true
$script:DeployStartTime = Get-Date
$script:DeployHead = 'head|run|' + $script:DeployStartTime.ToString('yyyy-MM-dd HH:mm:ss')
Save-DeployStatus

# 1. 設定をそろえる
Enter-DeployItem 'apply'
Set-DeployStep 1 'スリープ・背景・通知・Windows Update などを講習用にそろえています'
$r1 = New-Object System.Collections.Generic.List[object]
try { Invoke-ApplyAll $prof $r1 } catch { [void]$r1.Add((New-Row 'apply' '設定をそろえる' 'FAIL' '' '' ('途中で問題が起きました：' + $_.Exception.Message))) }
Add-Rows 'そろえる' $r1
Exit-ItemByRows $r1 '完了しました（変えた {0} 件）'

# 2. アプリの削除とインストール
$r2 = New-Object System.Collections.Generic.List[object]
if ($steps.Count -gt 0) {
    Invoke-DeployPlan $steps $prof $profileDir $r2
    Add-Rows '準備' $r2
}

# 3. 履歴を消す（最後）
Enter-DeployItem 'clean'
if (Test-Cancelled) {
    Exit-DeployItem 'skip' '取り消されたため行いませんでした'
} else {
    Set-DeployStep 1 '最近使ったファイル・ダウンロード・IME・ブラウザーなどを消しています'
    $r3 = New-Object System.Collections.Generic.List[object]
    try { Invoke-CleanAll $prof $r3 } catch { [void]$r3.Add((New-Row 'clean' '履歴を消す' 'FAIL' '' '' ('途中で問題が起きました：' + $_.Exception.Message))) }
    Add-Rows '履歴を消す' $r3
    Exit-ItemByRows $r3 '完了しました（消した項目 {0}）'
}

Add-Rows 'まとめて' @((New-Row 'total' 'まとめて実行（全体）' 'INFO' '' '' ('所要 ' + (Format-Duration ((Get-Date) - $script:DeployStartTime).TotalSeconds))))
Finish 0

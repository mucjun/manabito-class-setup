# まなびと講習準備 ― 時間のかかる準備（Office・Adobe CC・独自アプリ）
#
# 画面のアプリから「管理者として」呼ばれる。時刻を指定したときは、ここで開始時刻まで待つ
# （夜中に管理者の確認が出ても答えられないため、確認は押したときに 1 回だけ）。
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File deploy.ps1
#       -ProfilePath <プロファイル.json> -OutFile <結果.tsv> -LogFile <記録.txt>
#       -StatusFile <状況.txt> -CancelFile <取り消し.flag> -Office 1 -Adobe 0 -OwnApps 1 [-StartAt "yyyy-MM-dd HH:mm"]
#
# -Office・-Adobe：0=何もしない 1=入れる 2=メーカーの削除ツールで削除する（入っているときだけ）
#
# 状況.txt は画面のアプリが 1 秒ごとに読んで表示する。取り消し.flag ができたら、区切りのよいところでやめる。

param(
    [string]$ProfilePath = '',
    [Parameter(Mandatory = $true)][string]$OutFile,
    [string]$LogFile = '',
    [string]$StatusFile = '',
    [string]$CancelFile = '',
    [int]$Office = 0,
    [int]$Adobe = 0,
    [int]$OwnApps = 0,
    [string]$OnlyApps = '',
    [string]$StartAt = ''
)

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($LogFile) { try { Start-Transcript -LiteralPath $LogFile -Force | Out-Null } catch { } }

foreach ($m in @('common', 'set', 'deploy')) {
    . (Join-Path $here ('modules\' + $m + '.ps1'))
}
$script:DeployStatusFile = $StatusFile
$script:OnlyAppsFilter = $OnlyApps
$script:DeployCancelFile = $CancelFile

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("#区分`tID`t項目`t結果`t前`t後`t説明")

function Clean-Field {
    param($Value)
    if ($null -eq $Value) { return '' }
    return ([string]$Value) -replace "[`t`r`n]+", ' '
}

function Add-Result {
    param($Row)
    $fields = @('準備', $Row.Id, $Row.Name, $Row.State, $Row.Current, $Row.Expected, $Row.Detail) | ForEach-Object { Clean-Field $_ }
    $lines.Add(($fields -join "`t"))
    Write-Host ('[{0}] {1} ／ {2} → {3} ／ {4}' -f $Row.State, $Row.Name, $Row.Current, $Row.Expected, $Row.Detail)
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

if (-not (Test-IsAdmin)) {
    Add-Result (New-Row 'admin' '始める前の確認' 'FAIL' '' '' '管理者として動いていないため中止しました。')
    Finish 2
}

$prof = $null
$profileDir = Join-Path (Split-Path -Parent $here) 'profiles'
if ($ProfilePath) {
    try {
        $prof = Get-Content -LiteralPath $ProfilePath -Raw -Encoding UTF8 -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        $profileDir = Split-Path -Parent $ProfilePath
    } catch {
        Add-Result (New-Row 'profile' '始める前の確認' 'FAIL' '' '' ('プロファイルを読めませんでした：' + $_.Exception.Message))
        Finish 2
    }
}

Set-KeepAwake $true

# 画面に出す作業の一覧（手順の数）を先に登録する
$steps = Register-DeployPlan $Office $Adobe $OwnApps
$script:DeployHead = 'head|run|' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')

if ($StartAt) {
    $when = [datetime]::ParseExact($StartAt, 'yyyy-MM-dd HH:mm', [Globalization.CultureInfo]::InvariantCulture)
    Write-Host ('開始時刻: ' + $when)
    if (-not (Wait-Until $when)) {
        Add-Result (New-Row 'cancel' '時間のかかる準備' 'SKIP' '' '' '開始前に取り消されました。何もしていません。')
        Finish 0
    }
}

# 別の作業が動いていたら、終わるまで待つ（最大 30 分）。開始待ちの間は順番を取らない。
if (-not (Enter-WorkLock 0)) {
    foreach ($it in $script:DeployItems) { $it.Text = '別の作業が終わるのを待っています' }
    Save-DeployStatus
    $got = $false
    for ($i = 0; $i -lt 30 -and -not $got; $i++) {
        if (Test-Cancelled) { break }
        $got = Enter-WorkLock 60
    }
    if (-not $got) {
        foreach ($it in $script:DeployItems) { $it.State = 'skip'; $it.Text = '別の作業が終わらないため、行いませんでした' }
        Add-Result (New-Row 'busy' '始める前の確認' 'FAIL' '' '' '別の作業（設定をそろえる・履歴を消す・時間のかかる準備）が 30 分たっても終わらないため中止しました。別の講習準備アプリが開いていないか確かめてください。')
        Finish 2
    }
    foreach ($it in $script:DeployItems) { $it.Text = '' }
}

$script:DeployStartTime = Get-Date
$script:DeployHead = 'head|run|' + $script:DeployStartTime.ToString('yyyy-MM-dd HH:mm:ss')
Save-DeployStatus
$rows = New-Object System.Collections.Generic.List[object]
Invoke-DeployPlan $steps $prof $profileDir $rows
foreach ($r in $rows) { Add-Result $r }

if ($steps.Count -gt 0) {
    Add-Result (New-Row 'total' '時間のかかる準備（全体）' 'INFO' '' '' ('所要 ' + (Format-Duration ((Get-Date) - $script:DeployStartTime).TotalSeconds)))
}
Finish 0

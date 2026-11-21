# まなびと講習準備 ― 安全と更新の点検

function Get-WindowsEditionInfo {
    $k = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $ed = [string](Get-RegValue $k 'EditionID')
    $build = [int](Get-RegValue $k 'CurrentBuild')
    $ubr = Get-RegValue $k 'UBR'
    $disp = [string](Get-RegValue $k 'DisplayVersion')
    $edName = switch -Regex ($ed) {
        '^Core'         { 'Home'; break }
        '^Professional' { 'Pro'; break }
        '^Enterprise'   { 'Enterprise'; break }
        '^Education'    { 'Education'; break }
        default         { $ed }
    }
    $os = 'Windows 10'
    if ($build -ge 22000) { $os = 'Windows 11' }   # ProductName は 11 でも「Windows 10」のままのことがある
    return [pscustomobject]@{
        Os = $os; Edition = $edName; EditionId = $ed
        IsHome = ($ed -like 'Core*'); Build = $build; Ubr = $ubr; Display = $disp
    }
}

function Test-Edition {
    param($Prof)
    $e = Get-WindowsEditionInfo
    $cur = ('{0} {1} {2}（ビルド {3}.{4}）' -f $e.Os, $e.Edition, $e.Display, $e.Build, $e.Ubr)
    $detail = ''
    if ($e.IsHome) { $detail = 'Home のため、Windows Update は「止める」ではなく「一時停止（最大 5 週間）」で代用します。' }
    return New-Row 'edition' 'Windows の種類' 'INFO' $cur '' $detail
}

function Test-SmartAppControl {
    param($Prof)
    $v = Get-RegValue 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy' 'VerifiedAndReputablePolicyState'
    $howto = '「Windows セキュリティ」→「アプリとブラウザーの制御」→「スマート アプリ コントロールの設定」→「オフ」。'
    switch ($v) {
        0 { return New-Row 'sac' 'スマート アプリ コントロール' 'OK' 'オフ' 'オフ' '独自アプリ（署名なし）を動かせる状態です。' }
        1 { return New-Row 'sac' 'スマート アプリ コントロール' 'NG' 'オン' 'オフ' ('独自アプリが止められます。' + $howto) }
        2 { return New-Row 'sac' 'スマート アプリ コントロール' 'NG' '評価中' 'オフ' ('評価中は、ある日突然「オン」に切り替わることがあります。' + $howto) }
        default {
            return New-Row 'sac' 'スマート アプリ コントロール' 'NA' 'この Windows には無い' 'オフ' ('VerifiedAndReputablePolicyState={0}' -f $v)
        }
    }
}

function Test-WindowsUpdate {
    param($Prof)
    $want = [string](Get-Pref $Prof 'windowsUpdate' 'check-only')
    $e = Get-WindowsEditionInfo
    $au = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU'
    $noAuto = Get-RegValue $au 'NoAutoUpdate'
    $auOpt = Get-RegValue $au 'AUOptions'
    $pauseText = Get-RegValue 'HKLM:\SOFTWARE\Microsoft\WindowsUpdate\UX\Settings' 'PauseUpdatesExpiryTime'
    $pausedUntil = $null
    if ($pauseText) {
        $d = [datetime]::MinValue
        if ([datetime]::TryParse([string]$pauseText, [ref]$d)) {
            if ($d.ToUniversalTime() -gt (Get-Date).ToUniversalTime()) { $pausedUntil = $d.ToLocalTime() }
        }
    }
    $stoppedByPolicy = ($noAuto -eq 1)
    $parts = @()
    if ($stoppedByPolicy) { $parts += '止めている（ポリシー）' }
    if ($pausedUntil) { $parts += ('一時停止中（{0:yyyy/MM/dd} まで）' -f $pausedUntil) }
    if ($parts.Count -eq 0) { $parts += '動いている' }
    $cur = $parts -join ' ／ '
    $detail = ('NoAutoUpdate={0} AUOptions={1} PauseUpdatesExpiryTime={2}' -f $noAuto, $auOpt, $pauseText)
    if ($stoppedByPolicy -and $e.IsHome) { $detail += ' ※ Home ではポリシーが効かないことがあります。' }

    $stopped = $stoppedByPolicy -or ($null -ne $pausedUntil)
    switch ($want) {
        'stop' {
            $state = 'NG'; if ($stopped) { $state = 'OK' }
            return New-Row 'wu' 'Windows Update' $state $cur '止める' $detail
        }
        'run' {
            $state = 'NG'; if (-not $stopped) { $state = 'OK' }
            return New-Row 'wu' 'Windows Update' $state $cur '動かす' $detail
        }
        default {
            return New-Row 'wu' 'Windows Update' 'INFO' $cur '確認のみ' $detail
        }
    }
}

# ---------------------------------------------------------------- Windows Update の未適用・Defender の定義・LAN での更新の分け合い

function Get-WuStatusPath { return (Join-Path $env:LOCALAPPDATA 'ManabitoClassSetup\wu_status.txt') }

# 起動時に裏で確かめた結果（scripts\wu_check.ps1）を読む。1 行目：done|日時|件数|うちセキュリティ ／ error|日時|理由、2 行目から更新の名前
function Test-WindowsUpdatePending {
    param($Prof)
    $name = 'Windows Update（まだ入っていない更新）'
    $p = $null; if ($env:LOCALAPPDATA) { $p = Get-WuStatusPath }
    if (-not $p -or -not (Test-Path -LiteralPath $p)) { return New-Row 'wupending' $name 'NA' 'まだ確かめていません' 'なし' 'アプリを起動すると、裏で確かめます（1〜数分）。' }
    $lines = @(Get-Content -LiteralPath $p -Encoding UTF8)
    $f = ([string]$lines[0]).TrimStart([char]0xFEFF).Split('|')   # 先頭の BOM（版によって残る）を取る
    if ($f[0] -ne 'done') { return New-Row 'wupending' $name 'NA' '確かめられませんでした' 'なし' ($f[2..($f.Count - 1)] -join ' ') }
    $total = [int]$f[2]; $sec = [int]$f[3]
    $state = 'OK'; if ($total -gt 0) { $state = 'NG' }
    $cur = '{0} 件（うちセキュリティ {1} 件）・{2} に確認' -f $total, $sec, $f[1]
    if ($total -eq 0) { $cur = 'なし・' + $f[1] + ' に確認' }
    $titles = @($lines | Select-Object -Skip 1 | Where-Object { $_ }) -join ' ／ '
    return New-Row 'wupending' $name $state $cur 'なし' ('品質更新・セキュリティ更新だけを数えています（機能更新・ドライバー・プレビュー・定義ファイルは除く）。' + $titles)
}

function Test-DefenderSignature {
    param($Prof)
    $name = 'ウイルス対策の定義（Microsoft Defender）'
    try { $st = Get-MpComputerStatus -ErrorAction Stop } catch { return New-Row 'defender' $name 'NA' '調べられません' '3 日以内' '' }
    $last = $st.AntivirusSignatureLastUpdated
    if (-not $last) { return New-Row 'defender' $name 'NA' '調べられません' '3 日以内' '' }
    $days = [int][Math]::Floor(((Get-Date) - $last).TotalDays)
    $state = 'OK'; if ($days -gt 3) { $state = 'NG' }
    return New-Row 'defender' $name $state ('{0} 日前（{1}）' -f $days, $last.ToString('yyyy/MM/dd HH:mm')) '3 日以内' ('定義の版 ' + $st.AntivirusSignatureVersion + '。古いときは Windows セキュリティ →「ウイルスと脅威の防止」→「保護の更新」→「更新プログラムのチェック」。')
}

function Test-DeliveryOptimization {
    param($Prof)
    $want = [string](Get-Pref $Prof 'deliveryOptimization' 'lan')
    $name = '更新の LAN での分け合い（配信の最適化）'
    $v = Get-RegValue 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\DeliveryOptimization' 'DODownloadMode'
    $cur = '既定'; if ($v -eq 1) { $cur = 'LAN の中で分け合う' } elseif ($null -ne $v) { $cur = ('設定 ' + $v) }
    if ($want -eq 'check-only') { return New-Row 'dolan' $name 'NA' $cur '確認のみ' '' }
    $state = 'NG'; if ($v -eq 1) { $state = 'OK' }
    return New-Row 'dolan' $name $state $cur 'LAN の中で分け合う' '1 台が受け取った Windows の更新を、同じ LAN のほかの PC が分けてもらえるようにします（回線の負担が減ります）。'
}

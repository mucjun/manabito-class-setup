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

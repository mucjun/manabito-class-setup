# まなびと講習準備 ― 電源と画面の点検

# powercfg /q の出力から、今の電源プランの値（電源接続時・電池使用時）を取り出す。
# 出力は Windows の言語で変わるので、文字は見ずに「0x で始まる値の最後の 2 つ」を使う
# （最後の 2 つが AC と DC の現在値）。
function Get-PowerIndex {
    param([string]$Sub, [string]$Setting)
    # 機種によっては隠し項目になっていて /q に出てこないので、そのときは /qh（隠し項目も出す）で読む
    $out = & powercfg.exe /q SCHEME_CURRENT $Sub $Setting 2>$null
    if ($LASTEXITCODE -ne 0 -or $null -eq $out -or -not (($out -join "`n") -match '0x[0-9a-fA-F]{8}')) {
        $out = & powercfg.exe /qh SCHEME_CURRENT $Sub $Setting 2>$null
        if ($LASTEXITCODE -ne 0 -or $null -eq $out) { return $null }
    }
    $vals = New-Object System.Collections.Generic.List[uint32]
    foreach ($line in @($out)) {
        foreach ($m in [regex]::Matches([string]$line, '0x([0-9a-fA-F]{8})')) {
            $vals.Add([Convert]::ToUInt32($m.Groups[1].Value, 16))
        }
    }
    if ($vals.Count -lt 2) { return $null }
    return @{ AC = $vals[$vals.Count - 2]; DC = $vals[$vals.Count - 1] }
}

# 「しない（0）」であるべき時間の設定を判定する共通処理
function Test-PowerTimeout {
    param([string]$Id, [string]$Name, [string]$Sub, [string]$Setting)
    $v = Get-PowerIndex $Sub $Setting
    if ($null -eq $v) {
        return New-Row $Id $Name 'ERR' '' 'しない' ('powercfg で {0} を読めませんでした。' -f $Setting)
    }
    $battery = Test-HasBattery
    $cur = '電源接続時: ' + (Format-Minutes $v.AC)
    if ($battery) { $cur += ' ／ 電池: ' + (Format-Minutes $v.DC) }
    $ok = ($v.AC -eq 0) -and ((-not $battery) -or $v.DC -eq 0)
    $state = 'NG'; if ($ok) { $state = 'OK' }
    $detail = ('powercfg {0} {1}: AC={2} 秒, DC={3} 秒' -f $Sub, $Setting, $v.AC, $v.DC)
    return New-Row $Id $Name $state $cur 'しない' $detail
}

function Test-Sleep {
    param($Prof)
    Test-PowerTimeout 'sleep' '自動スリープ' 'SUB_SLEEP' 'STANDBYIDLE'
}

function Test-DisplayOff {
    param($Prof)
    Test-PowerTimeout 'display' '画面の自動消灯' 'SUB_VIDEO' 'VIDEOIDLE'
}

function Test-Hibernate {
    param($Prof)
    $v = Get-PowerIndex 'SUB_SLEEP' 'HIBERNATEIDLE'
    if ($null -eq $v) {
        return New-Row 'hibernate' '自動の休止状態' 'INFO' '設定が見つからない' 'しない' '休止状態が無効の PC では、この設定が出てこないことがあります。'
    }
    Test-PowerTimeout 'hibernate' '自動の休止状態' 'SUB_SLEEP' 'HIBERNATEIDLE'
}

function Test-LidAction {
    param($Prof)
    if (-not (Test-HasBattery)) {
        return New-Row 'lid' 'ふたを閉じたとき' 'NA' 'デスクトップ PC' '何もしない' '電池が見つからないため、ふたの無い PC とみなしました。'
    }
    $v = Get-PowerIndex 'SUB_BUTTONS' 'LIDACTION'
    if ($null -eq $v) {
        return New-Row 'lid' 'ふたを閉じたとき' 'ERR' '' '何もしない' 'powercfg で LIDACTION を読めませんでした。'
    }
    $names = @('何もしない', 'スリープ', '休止状態', 'シャットダウン')
    $ac = if ($v.AC -lt 4) { $names[$v.AC] } else { [string]$v.AC }
    $dc = if ($v.DC -lt 4) { $names[$v.DC] } else { [string]$v.DC }
    $state = 'NG'; if ($v.AC -eq 0) { $state = 'OK' }
    return New-Row 'lid' 'ふたを閉じたとき' $state ('電源接続時: {0} ／ 電池: {1}' -f $ac, $dc) '電源接続時は何もしない' '夜間の作業は電源につないで行う前提のため、電源接続時の動作だけを見ています。'
}

# 電源の状態と、待機の方式（モダンスタンバイかどうか）
function Test-PowerSource {
    param($Prof)
    if (-not (Test-HasBattery)) {
        $cur = 'デスクトップ PC（電池なし）'
    } else {
        $online = $null
        try {
            $bs = @(Get-CimInstance -Namespace 'root\wmi' -ClassName BatteryStatus -ErrorAction Stop)
            if ($bs.Count -gt 0) { $online = [bool]$bs[0].PowerOnline }
        } catch { }
        if ($null -eq $online) {
            $b = @(Get-CimInstance -ClassName Win32_Battery -ErrorAction SilentlyContinue)
            if ($b.Count -gt 0) { $online = ($b[0].BatteryStatus -ne 1) }
        }
        if ($online -eq $true) { $cur = 'ノート PC・電源につながっている' }
        elseif ($online -eq $false) { $cur = 'ノート PC・電池で動いている' }
        else { $cur = 'ノート PC・電源の状態は不明' }
    }

    # powercfg /a の最初の段落が「使える待機状態」。そこに S0 があればモダンスタンバイ。
    $standby = '不明'
    $out = @(& powercfg.exe /a 2>$null)
    if ($LASTEXITCODE -eq 0 -and $out.Count -gt 0) {
        $first = New-Object System.Collections.Generic.List[string]
        $started = $false
        foreach ($line in $out) {
            $s = [string]$line
            if ($s.Trim() -eq '') { if ($started) { break } else { continue } }
            $started = $true
            $first.Add($s)
        }
        if (($first -join "`n") -match 'S0') { $standby = 'モダンスタンバイ（画面が消えると待機に入る機種）' }
        else { $standby = '通常のスリープ' }
        Add-Detail '--- powercfg /a ---'
        foreach ($line in $out) { Add-Detail ([string]$line) }
    }
    return New-Row 'power' '電源と待機の方式' 'INFO' ($cur + ' ／ ' + $standby) '' 'モダンスタンバイの機種は、夜間の作業中に画面を点けたままにする必要があります。'
}

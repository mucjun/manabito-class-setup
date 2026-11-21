# まなびと講習準備 ― PC の健康状態（空き容量・時刻のずれ・電池の劣化）
#
# 基準はプロファイルで変えられる：healthDiskMinGB（標準 10）・healthClockMaxSec（標準 120）・healthBatteryMinPct（標準 60）
# 行の Id（health-disk・health-clock・health-battery）は、先生用の窓への報告でも使う。

function Test-HealthDisk {
    param($Prof)
    $name = '空き容量（C ドライブ）'
    $min = [double](Get-Pref $Prof 'healthDiskMinGB' 10)
    try {
        $sys = $env:SystemDrive; if (-not $sys) { $sys = 'C:' }
        $d = Get-CimInstance -ClassName Win32_LogicalDisk -Filter ("DeviceID='{0}'" -f $sys) -ErrorAction Stop
        $free = [double]$d.FreeSpace / 1GB
        $size = [double]$d.Size / 1GB
    } catch { return New-Row 'health-disk' $name 'NA' '調べられません' ('{0} GB 以上' -f $min) '' }
    $state = 'OK'; if ($free -lt $min) { $state = 'NG' }
    return New-Row 'health-disk' $name $state ('{0:N1} GB（全体 {1:N0} GB）' -f $free, $size) ('{0} GB 以上' -f $min) '足りないときは、ごみ箱・ダウンロード・一時ファイル（設定 →「システム」→「記憶域」）を確かめてください。'
}

# インターネットの時刻（time.windows.com）とのずれ。w32tm（Windows のツール）で 1 回だけ測る
function Test-HealthClock {
    param($Prof)
    $name = '時刻のずれ'
    $max = [double](Get-Pref $Prof 'healthClockMaxSec' 120)
    if (-not $env:WINDIR) { return New-Row 'health-clock' $name 'NA' '調べられません' ('{0} 秒以内' -f $max) '' }
    $exe = Join-Path $env:WINDIR 'System32\w32tm.exe'
    if (-not (Test-Path -LiteralPath $exe)) { return New-Row 'health-clock' $name 'NA' '調べられません' ('{0} 秒以内' -f $max) '' }
    $offset = $null
    try {
        $out = @(& $exe /stripchart /computer:time.windows.com /samples:1 /dataonly 2>$null)
        foreach ($l in $out) {
            if ([string]$l -match ',\s*([+-]?\d+(?:\.\d+)?)s\s*$') { $offset = [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture) }
        }
    } catch { }
    if ($null -eq $offset) { return New-Row 'health-clock' $name 'NA' '測れません' ('{0} 秒以内' -f $max) 'インターネットの時刻（time.windows.com）につながらないときは測れません。' }
    $abs = [Math]::Abs($offset)
    $state = 'OK'; if ($abs -gt $max) { $state = 'NG' }
    return New-Row 'health-clock' $name $state ('{0:N0} 秒' -f $abs) ('{0} 秒以内' -f $max) '大きくずれていると、Microsoft や Adobe のサインインに失敗することがあります。設定 →「時刻と言語」→「日付と時刻」→「今すぐ同期」。'
}

# 電池の劣化：満充電の容量 ÷ 設計上の容量（powercfg の電池のレポート）。電池の無い PC は対象外
function Test-HealthBattery {
    param($Prof)
    $name = '電池の劣化'
    $min = [double](Get-Pref $Prof 'healthBatteryMinPct' 60)
    if (-not $env:WINDIR -or -not $env:TEMP) { return New-Row 'health-battery' $name 'NA' '調べられません' ('{0}% 以上' -f $min) '' }
    $exe = Join-Path $env:WINDIR 'System32\powercfg.exe'
    if (-not (Test-Path -LiteralPath $exe)) { return New-Row 'health-battery' $name 'NA' '調べられません' ('{0}% 以上' -f $min) '' }
    $xml = Join-Path $env:TEMP ('manabito_battery_' + [guid]::NewGuid().ToString('N') + '.xml')
    try {
        & $exe /batteryreport /xml /output $xml 2>$null | Out-Null
        if (-not (Test-Path -LiteralPath $xml)) { return New-Row 'health-battery' $name 'NA' '調べられません' ('{0}% 以上' -f $min) '' }
        [xml]$r = Get-Content -LiteralPath $xml -Raw -Encoding UTF8
        $bats = @($r.BatteryReport.Batteries.Battery)
        if ($bats.Count -eq 0 -or $null -eq $bats[0]) { return New-Row 'health-battery' $name 'NA' '電池なし' ('{0}% 以上' -f $min) '' }
        $worst = $null
        foreach ($b in $bats) {
            $design = [double]$b.DesignCapacity; $full = [double]$b.FullChargeCapacity
            if ($design -le 0) { continue }
            $pct = [Math]::Round($full * 100 / $design)
            if ($null -eq $worst -or $pct -lt $worst) { $worst = $pct }
        }
        if ($null -eq $worst) { return New-Row 'health-battery' $name 'NA' '調べられません' ('{0}% 以上' -f $min) '' }
        $state = 'OK'; if ($worst -lt $min) { $state = 'NG' }
        $cur = ('{0}%' -f $worst); if ($worst -lt $min) { $cur += '（劣化）' } else { $cur += '（良好）' }
        return New-Row 'health-battery' $name $state $cur ('{0}% 以上' -f $min) '満充電の容量が、新品のときの何 % かを表します。少ないと、電源につながないとすぐに切れます。'
    } catch {
        return New-Row 'health-battery' $name 'NA' '調べられません' ('{0}% 以上' -f $min) ''
    } finally {
        if (Test-Path -LiteralPath $xml) { Remove-Item -LiteralPath $xml -Force -ErrorAction SilentlyContinue }
    }
}

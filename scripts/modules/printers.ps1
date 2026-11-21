# まなびと講習準備 ― 先生用から受け取ったプリンターの設定を、この PC に入れる・点検する
#
# 設定のファイル：%LOCALAPPDATA%\ManabitoClassSetup\printers.txt
#   画面のアプリ（src\share.cpp）が、先生の PC の署名を、登録した鍵で確かめてから書く。確かめられないものは書かない。
#   1 行目 v|版、2 行目から p|名前|ホスト|方式（raw・ipp）|ポート番号|ドライバー|既定（1/0）
# ドライバーは、この PC にもうあるもの（Windows に入っているものを含む）だけを使う。ファイルは受け取らない。
# 設定に無いプリンターは消さない（教室で足したものを壊さないため）。

function Get-PrinterConfigPath {
    if (-not $env:LOCALAPPDATA) { return $null }
    return (Join-Path $env:LOCALAPPDATA 'ManabitoClassSetup\printers.txt')
}

# 設定を読み、1 行ずつ確かめる（形が違う行は使わない）。戻り値：@{ Version; Items（正しい行）; Bad（使わなかった行の数）}
function Read-PrinterConfig {
    $p = Get-PrinterConfigPath
    if (-not $p -or -not (Test-Path -LiteralPath $p)) { return $null }
    $items = @(); $bad = 0; $ver = ''
    foreach ($line in @(Get-Content -LiteralPath $p -Encoding UTF8)) {
        $l = ([string]$line).TrimStart([char]0xFEFF).TrimEnd()
        if (-not $l) { continue }
        $f = $l.Split('|')
        if ($f[0] -eq 'v' -and $f.Count -ge 2) { $ver = $f[1]; continue }
        if ($f[0] -ne 'p' -or $f.Count -lt 7) { $bad++; continue }
        $it = [pscustomobject]@{ Name = $f[1]; Host = $f[2]; Proto = $f[3]; Port = 0; Driver = $f[5]; Default = ($f[6] -eq '1') }
        $port = 0
        if (-not [int]::TryParse($f[4], [ref]$port) -or $port -lt 1 -or $port -gt 65535) { $bad++; continue }
        $it.Port = $port
        # 名前・ドライバーに制御文字や Windows で使えない文字が無いか。ホストは IPv4 か、ホスト名に使える文字だけ
        if (-not $it.Name -or $it.Name.Length -gt 120 -or $it.Name -match '[\x00-\x1f\\,!]') { $bad++; continue }
        if (-not $it.Driver -or $it.Driver.Length -gt 200 -or $it.Driver -match '[\x00-\x1f]') { $bad++; continue }
        if ($it.Host -notmatch '^[A-Za-z0-9]([A-Za-z0-9.\-]{0,250})$') { $bad++; continue }
        if ($it.Proto -notin @('raw', 'ipp')) { $bad++; continue }
        $items += $it
    }
    return @{ Version = $ver; Items = $items; Bad = $bad }
}

function Get-DefaultPrinterName {
    try { return [string](Get-CimInstance -ClassName Win32_Printer -Filter 'Default=TRUE' -ErrorAction Stop | Select-Object -First 1).Name } catch { return '' }
}

function Test-Printers {
    param($Prof)
    $name = 'プリンター（先生用から受け取る）'
    $cfg = Read-PrinterConfig
    if ($null -eq $cfg) { return New-Row 'printers' $name 'NA' '受け取っていません' '' '先生用の窓で配ったプリンターを、この PC が受け取ると点検します（先生の PC の登録が要ります）。' }
    if ($cfg.Items.Count -eq 0) { return New-Row 'printers' $name 'NA' '配られたプリンターなし' '' '' }
    $missing = @(); $have = @()
    foreach ($it in $cfg.Items) {
        $p = $null
        try { $p = Get-Printer -Name $it.Name -ErrorAction Stop } catch { }
        if ($p) { $have += $it.Name } else { $missing += $it.Name }
    }
    $def = Get-DefaultPrinterName
    $wantDef = @($cfg.Items | Where-Object { $_.Default } | Select-Object -First 1)
    $defOk = ($wantDef.Count -eq 0 -or $def -eq $wantDef[0].Name)
    $state = 'OK'; if ($missing.Count -gt 0 -or -not $defOk) { $state = 'NG' }
    $cur = @()
    if ($have.Count) { $cur += ($have -join '・') }
    if ($missing.Count) { $cur += ('入っていない：' + ($missing -join '・')) }
    $cur += ('既定：' + $(if ($def) { $def } else { 'なし' }))
    $exp = (@($cfg.Items | ForEach-Object { $_.Name }) -join '・')
    if ($wantDef.Count) { $exp += '（既定：' + $wantDef[0].Name + '）' }
    $detail = '先生用から受け取った設定（版 ' + $cfg.Version + '）。'
    if ($cfg.Bad -gt 0) { $detail += ('形の違う行 {0} 個は使っていません。' -f $cfg.Bad) }
    return New-Row 'printers' $name $state ($cur -join ' ／ ') $exp $detail
}

function Set-Printers {
    param($Prof)
    $name = 'プリンター（先生用から受け取る）'
    $cfg = Read-PrinterConfig
    if ($null -eq $cfg -or $cfg.Items.Count -eq 0) { return New-Row 'printers' $name 'SKIP' '' '' '先生用から受け取ったプリンターはありません。' }
    if (-not (Get-Command Add-Printer -ErrorAction SilentlyContinue)) { return New-Row 'printers' $name 'SKIP' '' '' 'この PC ではプリンターの命令が使えません。' }
    $done = @(); $same = @(); $fail = @()
    foreach ($it in $cfg.Items) {
        try {
            # ドライバー：この PC にあるもの（ドライバー ストアにあるものは入れる）。ファイルは受け取らない
            if (-not (Get-PrinterDriver -Name $it.Driver -ErrorAction SilentlyContinue)) {
                try { Add-PrinterDriver -Name $it.Driver -ErrorAction Stop }
                catch { $fail += ($it.Name + '（ドライバー「' + $it.Driver + '」がこの PC にありません）'); continue }
            }
            $existing = Get-Printer -Name $it.Name -ErrorAction SilentlyContinue
            if ($it.Proto -eq 'ipp') {
                $url = 'http://{0}:{1}/ipp/print' -f $it.Host, $it.Port
                if ($existing) { $same += $it.Name; continue }   # IPP は作り直さない（既にあれば、そのまま）
                Add-Printer -Name $it.Name -IppURL $url -ErrorAction Stop
                $done += $it.Name
                continue
            }
            $portName = 'IP_{0}' -f $it.Host
            if ($it.Port -ne 9100) { $portName = 'IP_{0}_{1}' -f $it.Host, $it.Port }
            if (-not (Get-PrinterPort -Name $portName -ErrorAction SilentlyContinue)) {
                Add-PrinterPort -Name $portName -PrinterHostAddress $it.Host -PortNumber $it.Port -ErrorAction Stop
            }
            if ($existing) {
                if ($existing.PortName -eq $portName -and $existing.DriverName -eq $it.Driver) { $same += $it.Name; continue }
                Set-Printer -Name $it.Name -PortName $portName -DriverName $it.Driver -ErrorAction Stop
            } else {
                Add-Printer -Name $it.Name -DriverName $it.Driver -PortName $portName -ErrorAction Stop
            }
            $done += $it.Name
        } catch {
            $fail += ($it.Name + '（' + $_.Exception.Message + '）')
        }
    }
    # 既定：Windows が勝手に既定を変えないようにしてから、選ばれたものを既定にする
    $note = ''
    $wantDef = @($cfg.Items | Where-Object { $_.Default } | Select-Object -First 1)
    if ($wantDef.Count -and (Get-Printer -Name $wantDef[0].Name -ErrorAction SilentlyContinue)) {
        try {
            Set-RegDword 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Windows' 'LegacyDefaultPrinterMode' 1
            $w = Get-CimInstance -ClassName Win32_Printer -Filter ("Name='{0}'" -f ($wantDef[0].Name -replace "'", "''")) -ErrorAction Stop
            $r = Invoke-CimMethod -InputObject $w -MethodName SetDefaultPrinter -ErrorAction Stop
            if ($r.ReturnValue -eq 0) { $note = '既定：' + $wantDef[0].Name + '。' } else { $fail += ('既定にできませんでした（' + $r.ReturnValue + '）') }
        } catch { $fail += ('既定にできませんでした（' + $_.Exception.Message + '）') }
    }
    $state = 'DONE'
    if ($fail.Count -gt 0) { $state = 'FAIL' } elseif ($done.Count -eq 0) { $state = 'SAME' }
    $detail = $note
    if ($done.Count) { $detail += '入れたもの：' + ($done -join '・') + '。' }
    if ($fail.Count) { $detail += 'うまくいかなかったもの：' + ($fail -join '、') + '。' }
    return New-Row 'printers' $name $state '' (@($cfg.Items | ForEach-Object { $_.Name }) -join '・') $detail
}

# まなびと講習準備 ― 時間のかかる準備（ダウンロード・インストール）
#
# 注意：関数の中では Write-Output を使わない（戻り値に混ざるため）。記録には Write-Host を使う。
# 注意：$script: の変数名を、呼び出す側の引数名（-StatusFile など）と重ねないこと。
#
# Office：Office 展開ツール（setup.exe /configure office.xml）
# Adobe：Creative Cloud デスクトップアプリのインストーラー（--silent）
# 独自アプリ：目録（apps.json）に書いた ZIP を入手し、SHA-256 を確かめて Program Files\Org_Apps へ展開
#
# 各関数は 1 行の結果を返す。State は DONE / SAME / SKIP / FAIL。

$script:DeployStatusFile = ''
$script:DeployCancelFile = ''
$script:WorkDir = Join-Path $env:ProgramData 'ManabitoClassSetup\work'

$script:DeployStartTime = $null      # 作業を始めた時刻

function Format-Duration {
    param([double]$Seconds)
    if ($Seconds -lt 0) { $Seconds = 0 }
    $s = [int][Math]::Round($Seconds)
    if ($s -lt 60) { return ('{0} 秒' -f $s) }
    if ($s -lt 3600) { return ('{0} 分 {1} 秒' -f [int][Math]::Floor($s / 60), ($s % 60)) }
    return ('{0} 時間 {1} 分' -f [int][Math]::Floor($s / 3600), [int][Math]::Floor(($s % 3600) / 60))
}

function Format-Bytes {
    param([double]$Bytes)
    if ($Bytes -ge 1GB) { return ('{0:N2} GB' -f ($Bytes / 1GB)) }
    if ($Bytes -ge 1MB) { return ('{0:N1} MB' -f ($Bytes / 1MB)) }
    return ('{0:N0} KB' -f ($Bytes / 1KB))
}

# 速さ：MB/秒と、回線の表記に近い Mbps の両方
function Format-Speed {
    param([double]$BytesPerSec)
    return ('{0:N1} MB/秒（{1:N0} Mbps）' -f ($BytesPerSec / 1MB), ($BytesPerSec * 8 / 1000000))
}

# ---- 状況ファイル（画面のアプリが 1 秒ごとに読んで、作業ごとの行として表示する）
# 1 行目：head|wait|開始予定（yyyy-MM-dd HH:mm）  または  head|run|開始時刻  または  head|done|開始時刻
# 2 行目から：item|ID|名前|状態|手順|手順の数|手順の名前|割合（-1 は不明）|説明
#   状態：wait（待機）run（作業中）done（完了）same（そのまま）fail（失敗）skip（見送り）
$script:DeployItems = New-Object System.Collections.ArrayList
$script:DeployCurrent = $null
$script:DeployHead = ''

function ConvertTo-StatusField {
    param($Value)
    if ($null -eq $Value) { return '' }
    return ([string]$Value) -replace '[|\r\n\t]+', ' '
}

function Save-DeployStatus {
    if (-not $script:DeployStatusFile) { return }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add($script:DeployHead)
    foreach ($it in $script:DeployItems) {
        $f = @('item', $it.Id, $it.Name, $it.State, $it.Step, $it.Steps, $it.StepName, $it.Percent, $it.Text) | ForEach-Object { ConvertTo-StatusField $_ }
        $lines.Add(($f -join '|'))
    }
    try {
        $tmp = $script:DeployStatusFile + '.part'
        [System.IO.File]::WriteAllLines($tmp, [string[]]$lines.ToArray(), (New-Object System.Text.UTF8Encoding $true))
        Move-Item -LiteralPath $tmp -Destination $script:DeployStatusFile -Force
    } catch { }
}

function Register-DeployItem {
    param([string]$Id, [string]$Name, [int]$Steps)
    [void]$script:DeployItems.Add([pscustomobject]@{ Id = $Id; Name = $Name; State = 'wait'; Step = 0; Steps = $Steps; StepName = ''; Percent = -1; Text = '' })
}

function Enter-DeployItem {
    param([string]$Id)
    $script:DeployCurrent = $script:DeployItems | Where-Object { $_.Id -eq $Id } | Select-Object -First 1
    if ($script:DeployCurrent) { $script:DeployCurrent.State = 'run' }
    Save-DeployStatus
}

# 今の作業の手順を進める（手順の数が途中で分かるときは -Steps で直す）
function Set-DeployStep {
    param([int]$Step, [string]$StepName, [int]$Steps = 0)
    $c = $script:DeployCurrent
    if (-not $c) { Write-Host ('[手順] ' + $StepName); return }
    $c.Step = $Step; $c.StepName = $StepName; $c.Percent = -1; $c.Text = ''
    if ($Steps -gt 0) { $c.Steps = $Steps }
    Write-Host ('[手順] {0} {1}/{2} {3}' -f $c.Name, $Step, $c.Steps, $StepName)
    Save-DeployStatus
}

function Set-DeployProgress {
    param([double]$Percent, [string]$Text)
    $c = $script:DeployCurrent
    Write-Host ('[状況] ' + $Text)
    if (-not $c) { return }
    if ($Percent -ge 0) { $c.Percent = [int][Math]::Min([double]100, [Math]::Floor([double]$Percent)) } else { $c.Percent = -1 }
    $c.Text = $Text
    Save-DeployStatus
}

function Exit-DeployItem {
    param([string]$State, [string]$Text)
    $c = $script:DeployCurrent
    if (-not $c) { return }
    $c.State = $State; $c.Text = $Text
    if ($State -eq 'done' -or $State -eq 'same') { $c.Percent = 100; $c.Step = $c.Steps }
    $script:DeployCurrent = $null
    Save-DeployStatus
}

# 以前の書き方の互換：説明だけを書き換える
function Write-Status {
    param([string]$Text)
    if ($Text -like '待機|*') {
        $script:DeployHead = 'head|wait|' + $Text.Substring(3)
        Save-DeployStatus
        return
    }
    if ($Text -eq '終わりました') { return }
    $p = -1
    if ($script:DeployCurrent) { $p = $script:DeployCurrent.Percent }
    Set-DeployProgress $p $Text
}

function Test-Cancelled {
    return ($script:DeployCancelFile -and (Test-Path -LiteralPath $script:DeployCancelFile))
}

# スリープさせない（ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED）
# モダンスタンバイの機種は画面が消えると待機に入るため、画面も点けたままにする。
$script:PowerCode = @'
using System;
using System.Runtime.InteropServices;
namespace ManabitoPower {
    public static class Native {
        [DllImport("kernel32.dll")]
        public static extern uint SetThreadExecutionState(uint flags);
    }
}
'@

function Set-KeepAwake {
    param([bool]$On)
    try {
        if (-not ('ManabitoPower.Native' -as [type])) { Add-Type -TypeDefinition $script:PowerCode -Language CSharp }
        if ($On) { [ManabitoPower.Native]::SetThreadExecutionState([uint32]2147483651) | Out-Null }   # 0x80000003
        else { [ManabitoPower.Native]::SetThreadExecutionState([uint32]2147483648) | Out-Null }       # 0x80000000
    } catch { }
}

# 指定の時刻まで待つ。取り消されたら $false。
function Wait-Until {
    param([datetime]$StartAt)
    Write-Status ('待機|' + $StartAt.ToString('yyyy-MM-dd HH:mm'))
    while ((Get-Date) -lt $StartAt) {
        if (Test-Cancelled) { return $false }
        Start-Sleep -Seconds 5
    }
    return $true
}

$script:LastDownloadSpeed = 0   # 直前のダウンロードの平均の速さ（バイト/秒）

# 進み具合（大きさ・速さ・残り時間）を出しながらダウンロードする
function Invoke-DownloadWithProgress {
    param([string]$Url, [string]$Path, [string]$Label)
    $req = [System.Net.HttpWebRequest]::Create($Url)
    $req.AllowAutoRedirect = $true
    $req.UserAgent = 'ManabitoClassSetup'
    $req.Timeout = 60000
    $req.ReadWriteTimeout = 60000
    $resp = $req.GetResponse()
    $in = $null; $out = $null
    try {
        $total = [double]$resp.ContentLength      # 分からないときは -1
        $in = $resp.GetResponseStream()
        $out = [System.IO.File]::Create($Path)
        $buf = New-Object byte[] 262144
        $got = [double]0
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $lastT = 0.0; $lastB = [double]0; $speed = 0.0
        while (($n = $in.Read($buf, 0, $buf.Length)) -gt 0) {
            $out.Write($buf, 0, $n)
            $got += $n
            $t = $sw.Elapsed.TotalSeconds
            if ($t - $lastT -ge 1.0) {
                $now = ($got - $lastB) / ($t - $lastT)
                if ($speed -le 0) { $speed = $now } else { $speed = 0.7 * $speed + 0.3 * $now }   # なめらかにする
                $lastT = $t; $lastB = $got
                if ($total -gt 0) {
                    $left = ''
                    if ($speed -gt 0) { $left = '・あと約 ' + (Format-Duration (($total - $got) / $speed)) }
                    Set-DeployProgress ($got * 100 / $total) ('{0}　{1} / {2}・{3}{4}' -f $Label, (Format-Bytes $got), (Format-Bytes $total), (Format-Speed $speed), $left)
                } else {
                    Set-DeployProgress -1 ('{0}　{1}・{2}' -f $Label, (Format-Bytes $got), (Format-Speed $speed))
                }
                if (Test-Cancelled) { throw '取り消されました。' }
            }
        }
        $sec = [Math]::Max($sw.Elapsed.TotalSeconds, 0.001)
        $script:LastDownloadSpeed = $got / $sec
        Write-Host ('{0}: {1}、{2}、平均 {3}' -f $Label, (Format-Bytes $got), (Format-Duration $sec), (Format-Speed $script:LastDownloadSpeed))
    } finally {
        if ($out) { $out.Dispose() }
        if ($in) { $in.Dispose() }
        $resp.Dispose()
    }
}

$script:DownloadTries = 3   # 取り直す回数（起動時の更新の確認では 1 回にする）

function Get-Download {
    param([string]$Url, [string]$Path, [string]$Label)
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $tries = [Math]::Max(1, [int]$script:DownloadTries)
    for ($i = 1; $i -le $tries; $i++) {
        if (Test-Cancelled) { throw '取り消されました。' }
        try {
            if ($i -gt 1) { Write-Status ('{0} をもう一度ダウンロードします（{1} 回目）' -f $Label, $i) }
            Invoke-DownloadWithProgress $Url $Path $Label
            return
        } catch {
            if ($_.Exception.Message -like '*取り消されました*') { throw }
            Write-Host ('ダウンロード失敗: ' + $_.Exception.Message)
            if ($i -ge $tries) { throw ('ダウンロードできませんでした：' + $_.Exception.Message) }
            Start-Sleep -Seconds (30 * $i)
        }
    }
}

# 電子署名を確かめる。戻り値が空なら合格、空でなければ不合格の理由。
# 「Valid」なら合格。「UnknownError」（証明書の期限切れ・失効の確認ができない など）のときは、
# 改ざん（HashMismatch）ではないこと・発行元の名前・信頼できる元までたどれることを確かめて合格にする。
function Test-Signer {
    param([string]$Path, [string]$Publisher)
    # ファイルそのものの様子も残す（実行ファイルなら先頭が MZ）
    try {
        $fi = Get-Item -LiteralPath $Path
        $head = [System.IO.File]::ReadAllBytes($Path)[0..1]
        $mz = (($head[0] -eq 0x4D) -and ($head[1] -eq 0x5A))
        Write-Host ('ファイル: {0}／{1}／先頭 {2:X2} {3:X2}（{4}）／SHA-256 {5}' -f $fi.FullName, (Format-Bytes $fi.Length), $head[0], $head[1],
                    $(if ($mz) { '実行ファイル' } else { '実行ファイルではない' }), (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash)
    } catch { }
    $sig = Get-AuthenticodeSignature -LiteralPath $Path
    $cert = $sig.SignerCertificate
    $subj = ''; $notAfter = ''
    if ($cert) { $subj = $cert.Subject; $notAfter = $cert.NotAfter.ToString('yyyy/MM/dd') }
    Write-Host ('署名の確認: {0} → {1}（{2}）／発行元: {3}／証明書の期限: {4}' -f (Split-Path -Leaf $Path), $sig.Status, $sig.StatusMessage, $subj, $notAfter)
    if (-not $cert) { return ('署名を読み取れません（' + $sig.Status + '：' + $sig.StatusMessage + '）。') }
    if ($subj -notmatch [regex]::Escape($Publisher)) { return ('発行元が ' + $Publisher + ' ではありません（' + $subj + '）。') }
    if ($sig.Status -eq 'Valid') { return '' }
    if ($sig.Status -eq 'UnknownError') {
        $chain = New-Object System.Security.Cryptography.X509Certificates.X509Chain
        $chain.ChainPolicy.RevocationMode = [System.Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
        $chain.ChainPolicy.VerificationFlags = [System.Security.Cryptography.X509Certificates.X509VerificationFlags]::IgnoreNotTimeValid
        if ($chain.Build($cert)) {
            Write-Host '  → 期限切れ・失効の確認ができない以外の問題はなく、発行元も一致するため、受け入れます（改ざんはありません）。'
            return ''
        }
        $why = ($chain.ChainStatus | ForEach-Object { $_.StatusInformation.Trim() }) -join ' '
        return ('署名を確かめられませんでした（' + $sig.StatusMessage + ' ' + $why + '）。')
    }
    return ('署名が有効ではありません（' + $sig.Status + '：' + $sig.StatusMessage + '）。')
}


# PC 全体の受信量（バイト）。インストーラーが自分でダウンロードする量の目安に使う（ほかの通信も混ざる）
function Get-NetReceivedBytes {
    $sum = [double]0
    foreach ($nic in [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
        if ($nic.OperationalStatus -ne 'Up') { continue }
        if ($nic.NetworkInterfaceType -in @('Loopback', 'Tunnel')) { continue }
        try { $sum += $nic.GetIPv4Statistics().BytesReceived } catch { }
    }
    return $sum
}

# インストーラー・削除ツールを動かし、終わるまで経過時間・受信量・速さ・残り時間の目安を出す。
# $ExpectedBytes が 0 なら、残り時間は出さない。戻り値は終了コード。
# -Visible：画面を隠さない（操作を求める画面が出ることがあるインストーラー用）
# -DoneCheck：インストーラーが終わる前でも「入った」と分かる判定。分かったら -DoneGraceSeconds 待って先へ進む
# -StallMinutes：この分数たっても受信が -StallBytes に届かず、DoneCheck も成り立たなければ、止まっているとみなして打ち切る
$script:ProcessStalled = $false
$script:ProcessEndedEarly = $false
function Start-ProcessWithProgress {
    param([string]$FilePath, [string[]]$Arguments, [string]$WorkDir, [string]$Label, [double]$ExpectedBytes = 0,
          [switch]$Visible, [scriptblock]$DoneCheck = $null, [int]$DoneGraceSeconds = 60,
          [int]$StallMinutes = 0, [double]$StallBytes = 10MB)
    $script:ProcessStalled = $false
    $script:ProcessEndedEarly = $false
    $style = 'Hidden'; if ($Visible) { $style = 'Normal' }
    # 起動できなかったら、ここで止める（$p が空のまま待つと、終わりを夜通し待ち続けてしまう）
    $p = Start-Process -FilePath $FilePath -ArgumentList $Arguments -WorkingDirectory $WorkDir -PassThru -WindowStyle $style -ErrorAction Stop
    if ($null -eq $p) { throw ($Label + '：起動できませんでした。') }
    $null = $p.Handle   # 先に触っておかないと、終わったあと ExitCode が取れないことがある
    $start = Get-Date
    $limit = $start.AddHours(6)   # 念のための上限
    $base = Get-NetReceivedBytes
    $lastT = $start; $lastB = $base; $speed = 0.0
    $doneAt = $null
    while (-not $p.HasExited) {
        Start-Sleep -Seconds 2
        $now = Get-Date
        if ($now -gt $limit) { throw ($Label + '：6 時間たっても終わらないため、待つのをやめました（インストーラーは動いたままです）。') }
        if ($DoneCheck -and -not $doneAt -and (& $DoneCheck)) { $doneAt = $now; Write-Host ($Label + '：入ったことを確かめました。') }
        if ($doneAt -and ($now - $doneAt).TotalSeconds -ge $DoneGraceSeconds) {
            # 入ったのにインストーラーが終わらない（サインイン画面などを待っている）→ 閉じて先へ進む
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            $script:ProcessEndedEarly = $true
            break
        }
        if ($StallMinutes -gt 0 -and -not $doneAt -and ($now - $start).TotalMinutes -ge $StallMinutes -and
            ((Get-NetReceivedBytes) - $base) -lt $StallBytes) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            $script:ProcessStalled = $true
            Write-Host ($Label + '：' + $StallMinutes + ' 分たっても進まないため打ち切りました。')
            break
        }
        # 表示のための計算でエラーが起きても、インストーラーの見張りは止めない
        try {
            $b = Get-NetReceivedBytes
            $dt = ($now - $lastT).TotalSeconds
            if ($dt -gt 0) {
                $inst = [Math]::Max([double]0, [double](($b - $lastB) / $dt))
                if ($speed -le 0) { $speed = $inst } else { $speed = 0.8 * $speed + 0.2 * $inst }
            }
            $lastT = $now; $lastB = $b
            $recv = [Math]::Max([double]0, [double]($b - $base))   # 2 GB を超えても扱えるよう double で（v0.12.0 の不具合）
            $text = ('経過 {0}・受信 {1}' -f (Format-Duration ($now - $start).TotalSeconds), (Format-Bytes $recv))
            $pct = -1
            if ($ExpectedBytes -gt 0) {
                $text += (' / 約 ' + (Format-Bytes $ExpectedBytes))
                $pct = [Math]::Min([double]99, [double]($recv * 100 / $ExpectedBytes))   # 目安からの割合（100% は終わったときだけ）
                if ($recv -ge $ExpectedBytes) { $text += '・仕上げ中（目安を超えました）' }
                elseif ($speed -gt 100KB) { $text += ('・あと約 ' + (Format-Duration (($ExpectedBytes - $recv) / $speed)) + '（目安）') }
            }
            $text += ('・' + (Format-Speed $speed))
            Set-DeployProgress $pct $text
        } catch {
            Write-Host ('（進み具合の表示でエラー。作業は続けます）' + $_.Exception.Message)
        }
    }
    $p.WaitForExit(10000) | Out-Null
    $secs = ((Get-Date) - $start).TotalSeconds
    $script:LastProcessSummary = ('所要 {0}・受信 {1}' -f (Format-Duration $secs), (Format-Bytes ([Math]::Max([double]0, [double]((Get-NetReceivedBytes) - $base)))))
    $code = 0
    if ($p.HasExited) { $code = $p.ExitCode } else { $code = -1 }
    if ($script:ProcessEndedEarly) { $code = 0 }
    Write-Host ($Label + ': ' + $script:LastProcessSummary + '、終了コード ' + $code)
    return $code
}
$script:LastProcessSummary = ''

function Initialize-WorkDir {
    if (-not (Test-Path -LiteralPath $script:WorkDir)) { New-Item -ItemType Directory -Path $script:WorkDir -Force | Out-Null }
}

# ---------------------------------------------------------------- Office

function Install-Office {
    param($Prof, [string]$ProfileDir)
    $name = 'Microsoft 365（Office）'
    $ver = Get-RegValue 'HKLM:\SOFTWARE\Microsoft\Office\ClickToRun\Configuration' 'VersionToReport'
    if ($ver) { return New-Row 'office' $name 'SAME' ('入っている（' + $ver + '）') '入っている' '' }

    $url = [string](Get-Pref $Prof 'officeSetupUrl' 'https://officecdn.microsoft.com/pr/wsus/setup.exe')
    $xmlName = [string](Get-Pref $Prof 'officeConfig' 'office.xml')
    $xml = Join-Path $ProfileDir $xmlName
    if (-not (Test-Path -LiteralPath $xml)) { return New-Row 'office' $name 'FAIL' '入っていない' '入っている' ('構成ファイルが見つかりません：' + $xml) }

    Initialize-WorkDir
    $setup = Join-Path $script:WorkDir 'office-setup.exe'
    Set-DeployStep 1 '展開ツールのダウンロード'
    Get-Download $url $setup 'Office の展開ツール'
    Set-DeployStep 2 '電子署名の確認'
    $bad = Test-Signer $setup 'Microsoft Corporation'
    if ($bad) { return New-Row 'office' $name 'FAIL' '入っていない' '入っている' ('展開ツールを確かめられませんでした：' + $bad) }

    Set-DeployStep 3 'インストール（ダウンロードと展開）'
    $expect = [double](Get-Pref $Prof 'officeExpectedMB' 3500) * 1MB
    $code = Start-ProcessWithProgress $setup @('/configure', ('"' + $xml + '"')) $script:WorkDir 'Office をインストール中' $expect
    Set-DeployStep 4 '入ったことの確認'
    $after = Get-RegValue 'HKLM:\SOFTWARE\Microsoft\Office\ClickToRun\Configuration' 'VersionToReport'
    if ($code -ne 0 -or -not $after) {
        return New-Row 'office' $name 'FAIL' '入っていない' '入っている' ('展開ツールの終了コード ' + $code + '。' + $script:LastProcessSummary + '。記録は %TEMP% と C:\Windows\Temp の Office のログを見てください。')
    }
    return New-Row 'office' $name 'DONE' '入っていない' ('入っている（' + $after + '）') ('構成：' + $xmlName + '。' + $script:LastProcessSummary)
}

# ---------------------------------------------------------------- Adobe

function Get-AdobeCCPath {
    foreach ($p in @((Join-Path $env:ProgramFiles 'Adobe\Adobe Creative Cloud\ACC\Creative Cloud.exe'),
                     (Join-Path ${env:ProgramFiles(x86)} 'Adobe\Adobe Creative Cloud\ACC\Creative Cloud.exe'))) {
        if ($p -and (Test-Path -LiteralPath $p)) { return $p }
    }
    return $null
}

function Install-AdobeCC {
    param($Prof, [string]$ProfileDir)
    $name = 'Adobe Creative Cloud デスクトップアプリ'
    if (Get-AdobeCCPath) { return New-Row 'adobe' $name 'SAME' '入っている' '入っている' '' }
    $url = [string](Get-Pref $Prof 'adobeInstallerUrl' '')
    if (-not $url) { return New-Row 'adobe' $name 'FAIL' '入っていない' '入っている' 'プロファイルに adobeInstallerUrl がありません。' }

    Initialize-WorkDir
    $setup = Join-Path $script:WorkDir 'Creative_Cloud_Set-Up.exe'
    Set-DeployStep 1 'インストーラーのダウンロード'
    Get-Download $url $setup 'Creative Cloud のインストーラー'
    Set-DeployStep 2 '電子署名の確認'
    $bad = Test-Signer $setup 'Adobe'
    if ($bad) { return New-Row 'adobe' $name 'FAIL' '入っていない' '入っている' ('インストーラーを確かめられませんでした：' + $bad) }
    Set-DeployStep 3 'インストール'

    $expect = [double](Get-Pref $Prof 'adobeExpectedMB' 600) * 1MB
    # 個人向けのインストーラーは、--silent でもサインイン画面を出して待つことがある（Adobe コミュニティの報告）。
    # 画面は隠さず、入ったと分かったらサインイン画面を閉じて先へ進む。15 分進まなければ打ち切る。
    $code = Start-ProcessWithProgress $setup @('--silent') $script:WorkDir 'Creative Cloud デスクトップアプリをインストール中' $expect `
            -Visible -DoneCheck { [bool](Get-AdobeCCPath) } -DoneGraceSeconds 60 -StallMinutes 15 -StallBytes 10MB
    if ($script:ProcessEndedEarly) {
        Stop-Process -Name 'Creative Cloud' -Force -ErrorAction SilentlyContinue   # 入ったあとに出るサインイン画面
    }
    if ($script:ProcessStalled) {
        return New-Row 'adobe' $name 'FAIL' '入っていない' '入っている' ('15 分たっても進まないため打ち切りました。インストーラーがサインインなどの画面の操作を待っていた可能性があります。人がいるときに［今すぐ実行］で試すか、Adobe の管理コンソールで作ったパッケージを使ってください。' + $script:LastProcessSummary)
    }
    # インストーラーが先に終わって、裏で続きを入れることがあるので、最大 15 分待つ
    Set-DeployStep 4 '仕上げと確認'
    for ($i = 0; $i -lt 90 -and -not (Get-AdobeCCPath); $i++) {
        if (Test-Cancelled) { break }
        Set-DeployProgress -1 ('Creative Cloud の仕上げを待っています（{0}／最大 15 分）' -f (Format-Duration ($i * 10)))
        Start-Sleep -Seconds 10
    }
    if (-not (Get-AdobeCCPath)) {
        return New-Row 'adobe' $name 'FAIL' '入っていない' '入っている' ('インストーラーの終了コード ' + $code + '。' + $script:LastProcessSummary + '。入ったことを確かめられませんでした。')
    }
    return New-Row 'adobe' $name 'DONE' '入っていない' '入っている' ('サインインは各自で行います。' + $script:LastProcessSummary)
}

# ---------------------------------------------------------------- 削除（メーカーの削除ツール）

# 開いているプロセスの名前（この利用者のもの）
function Get-RunningNames {
    param([string[]]$Names)
    $sid = (Get-Process -Id $PID).SessionId
    return @(Get-Process -Name $Names -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sid } |
             ForEach-Object { $_.ProcessName } | Sort-Object -Unique)
}

# Office：マイクロソフトの Get Help コマンドライン ツール（GetHelpCmd.exe）の「Office のアンインストール」
# （SaRA のコマンドライン版は 2026 年 3 月に廃止。Get Help のツールは作成から 90 日で使えなくなるため、毎回入手する）
function Remove-Office {
    param($Prof, [string]$ProfileDir)
    $name = 'Microsoft 365（Office）の削除'
    $ver = Get-RegValue 'HKLM:\SOFTWARE\Microsoft\Office\ClickToRun\Configuration' 'VersionToReport'
    if (-not $ver) { return New-Row 'office-remove' $name 'SAME' '入っていない' '入っていない' '' }
    $before = '入っている（' + $ver + '）'
    $open = Get-RunningNames @('WINWORD', 'EXCEL', 'POWERPNT', 'OUTLOOK', 'ONENOTE', 'MSACCESS', 'MSPUB', 'lync')
    if ($open.Count -gt 0) {
        return New-Row 'office-remove' $name 'FAIL' $before '入っていない' ('Office が開いています（' + ($open -join '、') + '）。閉じてからもう一度実行してください。')
    }

    # 1. Get Help コマンドライン ツール（Office のアンインストール。残りかすまで消す）
    $why = Invoke-OfficeScrub $Prof
    $after = Get-RegValue 'HKLM:\SOFTWARE\Microsoft\Office\ClickToRun\Configuration' 'VersionToReport'
    if (-not $why -and -not $after) {
        return New-Row 'office-remove' $name 'DONE' $before '入っていない' ('Get Help の削除ツールで削除しました。作業のあとで PC を再起動してください。' + $script:LastProcessSummary)
    }
    if (-not $why) { $why = 'Get Help の削除ツールのあとも Office が残っていました。' }
    Write-Host ('Get Help の削除ツールでは削除できませんでした：' + $why)

    # 2. 控え：Office 展開ツールに「すべて削除」の構成を渡す（マイクロソフトが管理者向けに案内している方法）
    $why2 = Invoke-OfficeOdtRemove $Prof
    $after = Get-RegValue 'HKLM:\SOFTWARE\Microsoft\Office\ClickToRun\Configuration' 'VersionToReport'
    if (-not $why2 -and -not $after) {
        return New-Row 'office-remove' $name 'DONE' $before '入っていない' ('Get Help の削除ツールが使えなかったため（' + $why + '）、Office 展開ツールで削除しました。作業のあとで PC を再起動してください。' + $script:LastProcessSummary)
    }
    if (-not $why2) { $why2 = '展開ツールのあとも Office が残っていました。' }
    return New-Row 'office-remove' $name 'FAIL' $before '入っていない' ('Get Help の削除ツール：' + $why + '／Office 展開ツール：' + $why2 + '　手で削除するときは、設定アプリの「アプリ」から行ってください。')
}

# Get Help の削除ツールで Office を削除する。うまくいけば空、だめなら理由を返す。
function Invoke-OfficeScrub {
    param($Prof)
    $url = [string](Get-Pref $Prof 'officeRemoveToolUrl' 'https://aka.ms/SaRA_EnterpriseVersionFiles')
    Initialize-WorkDir
    $zip = Join-Path $script:WorkDir 'gethelp.zip'
    $dir = Join-Path $script:WorkDir 'gethelp'
    Set-DeployStep 1 '削除ツール（Get Help）のダウンロード'
    try { Get-Download $url $zip 'マイクロソフトの削除ツール（Get Help）' } catch { return ('ダウンロードできませんでした：' + $_.Exception.Message) }
    Set-DeployStep 2 '展開と電子署名の確認'
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    try { Expand-Archive -LiteralPath $zip -DestinationPath $dir -Force } catch { return ('ZIP を展開できませんでした：' + $_.Exception.Message) }
    Write-Host ('ZIP の中身: ' + ((Get-ChildItem -LiteralPath $dir -Recurse -File | ForEach-Object { $_.Name + '（' + (Format-Bytes $_.Length) + '）' }) -join '、'))
    $exe = @(Get-ChildItem -LiteralPath $dir -Recurse -File -Include 'GetHelpCmd.exe' | Select-Object -First 1)
    if ($exe.Count -eq 0) { $exe = @(Get-ChildItem -LiteralPath $dir -Recurse -File -Include 'SaRAcmd.exe' | Select-Object -First 1) }
    if ($exe.Count -eq 0) { return 'ダウンロードした中に GetHelpCmd.exe がありません。' }
    $bad = Test-Signer $exe[0].FullName 'Microsoft Corporation'
    if ($bad) { return ('削除ツールを確かめられませんでした：' + $bad) }

    Set-DeployStep 3 '削除（30 分ほどかかることがあります）'
    $code = Start-ProcessWithProgress $exe[0].FullName @('-S', 'OfficeScrubScenario', '-AcceptEula', '-OfficeVersion', 'All') `
            $exe[0].DirectoryName 'Office を削除中' 0
    Set-DeployStep 4 '消えたことの確認'
    $codes = @{ 6 = 'Office のアプリが開いています'; 68 = 'Office が見つかりませんでした'; 8 = '複数の版の Office が見つかりました';
                9 = 'Office を削除できませんでした'; 10 = '管理者として動いていません';
                66 = '指定した版の Office が見つかりません'; 67 = '版の指定が正しくありません' }
    if ($code -ne 0) {
        $w = $codes[[int]$code]; if (-not $w) { $w = '終了コード ' + $code }
        return $w
    }
    return ''
}

# 控え：Office 展開ツール（setup.exe）に <Remove All="TRUE"/> の構成を渡して削除する。うまくいけば空、だめなら理由。
function Invoke-OfficeOdtRemove {
    param($Prof)
    $c = $script:DeployCurrent
    if ($c) { $c.Steps = 7 }
    Set-DeployStep 5 '控えの方法：Office 展開ツールのダウンロード' 7
    $url = [string](Get-Pref $Prof 'officeSetupUrl' 'https://officecdn.microsoft.com/pr/wsus/setup.exe')
    Initialize-WorkDir
    $setup = Join-Path $script:WorkDir 'office-setup.exe'
    try { Get-Download $url $setup 'Office の展開ツール' } catch { return ('ダウンロードできませんでした：' + $_.Exception.Message) }
    $bad = Test-Signer $setup 'Microsoft Corporation'
    if ($bad) { return ('展開ツールを確かめられませんでした：' + $bad) }
    $xml = Join-Path $script:WorkDir 'office-remove.xml'
    $conf = @'
<Configuration>
  <Remove All="TRUE" />
  <Display Level="None" AcceptEULA="TRUE" />
</Configuration>
'@
    [System.IO.File]::WriteAllText($xml, $conf, (New-Object System.Text.UTF8Encoding $false))
    Set-DeployStep 6 '控えの方法：Office 展開ツールで削除'
    $code = Start-ProcessWithProgress $setup @('/configure', ('"' + $xml + '"')) $script:WorkDir 'Office を削除中（展開ツール）' 0
    Set-DeployStep 7 '消えたことの確認'
    if ($code -ne 0) { return ('展開ツールの終了コード ' + $code) }
    return ''
}

# Adobe：Creative Cloud のアンインストーラー（あれば）→ Creative Cloud Cleaner Tool（すべて削除）
function Remove-AdobeCC {
    param($Prof, [string]$ProfileDir)
    $name = 'Adobe Creative Cloud の削除'
    if (-not (Get-AdobeCCPath)) { return New-Row 'adobe-remove' $name 'SAME' '入っていない' '入っていない' '' }
    $url = [string](Get-Pref $Prof 'adobeCleanerUrl' '')
    if (-not $url) { return New-Row 'adobe-remove' $name 'FAIL' '入っている' '入っていない' 'プロファイルに adobeCleanerUrl がありません。' }

    # Cleaner Tool は、Adobe のアプリと裏で動くもの（サービスを含む）をすべて閉じてから使う決まり
    Set-DeployStep 1 'Adobe のアプリを閉じる'
    $adobeProcs = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -match '\\Adobe\\' })
    foreach ($ap in $adobeProcs) { Stop-Process -Id $ap.Id -Force -ErrorAction SilentlyContinue }
    if ($adobeProcs.Count -gt 0) { Write-Host ('Adobe のプロセスを閉じました: ' + (($adobeProcs | ForEach-Object { $_.ProcessName } | Sort-Object -Unique) -join ', ')) }

    $notes = @()
    $uninst = @((Join-Path ${env:ProgramFiles(x86)} 'Adobe\Adobe Creative Cloud\Utils\Creative Cloud Uninstaller.exe'),
                (Join-Path $env:ProgramFiles 'Adobe\Adobe Creative Cloud\Utils\Creative Cloud Uninstaller.exe')) |
              Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
    Set-DeployStep 2 'Creative Cloud のアンインストール'
    if ($uninst) {
        $u = Start-ProcessWithProgress $uninst @('-u') (Split-Path -Parent $uninst) 'Creative Cloud デスクトップアプリを削除中' 0
        $notes += ('アンインストーラーの終了コード ' + $u)
    }

    Initialize-WorkDir
    $cleaner = Join-Path $script:WorkDir 'AdobeCreativeCloudCleanerTool.exe'
    Set-DeployStep 3 '削除ツールのダウンロードと確認'
    Get-Download $url $cleaner 'Adobe の削除ツール（Cleaner Tool）'
    $bad = Test-Signer $cleaner 'Adobe'
    if ($bad) { return New-Row 'adobe-remove' $name 'FAIL' '入っている' '入っていない' ('削除ツールを確かめられませんでした：' + $bad) }
    Set-DeployStep 4 '削除ツールで残りを削除'
    $c = Start-ProcessWithProgress $cleaner @('--eulaAccepted=1', '--removeAll=ALL') $script:WorkDir 'Adobe の削除ツールで残りを消しています' 0
    $notes += ('Cleaner Tool の終了コード ' + $c)
    Set-DeployStep 5 '消えたことの確認'
    if (Get-AdobeCCPath) {
        return New-Row 'adobe-remove' $name 'FAIL' '入っている' '入っていない' (($notes -join '、') + '。まだ残っています。Cleaner Tool の記録（%LOCALAPPDATA%\Temp の Adobe Creative Cloud Cleaner Tool.log）を見てください。')
    }
    return New-Row 'adobe-remove' $name 'DONE' '入っている' '入っていない' (($notes -join '、') + '。作業のあとで PC を再起動してください。')
}

# ---------------------------------------------------------------- 独自アプリ

function Get-AppsManifest {
    param([string]$Source)
    if ($Source -match '^https?://') {
        Initialize-WorkDir
        $tmp = Join-Path $script:WorkDir 'apps.json'
        Get-Download $Source $tmp '独自アプリの目録'
        $text = Get-Content -LiteralPath $tmp -Raw -Encoding UTF8
    } else {
        $text = Get-Content -LiteralPath $Source -Raw -Encoding UTF8 -ErrorAction Stop
    }
    return ($text | ConvertFrom-Json -ErrorAction Stop)
}

function New-Shortcut {
    param([string]$Target, [string]$Name)
    $desk = [Environment]::GetFolderPath('CommonDesktopDirectory')
    $ws = New-Object -ComObject WScript.Shell
    $lnk = $ws.CreateShortcut((Join-Path $desk ($Name + '.lnk')))
    $lnk.TargetPath = $Target
    $lnk.WorkingDirectory = Split-Path -Parent $Target
    $lnk.Save()
}

function Install-OneApp {
    param($App, [string]$Root, [int]$Index)
    $name = [string]$App.name
    $id = 'ownapp' + $Index
    $folder = [string]$App.folder
    $exe = [string]$App.exe
    $ver = [string]$App.version
    if (-not $name -or -not $folder -or -not $exe -or -not $App.url) {
        return New-Row $id ($name + '（目録）') 'FAIL' '' '' '目録に name・folder・exe・url のどれかがありません。'
    }
    if (-not $App.sha256) { return New-Row $id $name 'FAIL' '' $ver '目録に sha256 がありません（確かめずに入れることはしません）。' }

    $dest = Join-Path $Root $folder
    $verFile = Join-Path $dest '.manabito_version'
    $cur = ''
    if (Test-Path -LiteralPath $verFile) { $cur = (Get-Content -LiteralPath $verFile -Raw -Encoding UTF8).Trim() }
    elseif (Test-Path -LiteralPath (Join-Path $dest $exe)) { $cur = (Get-Item -LiteralPath (Join-Path $dest $exe)).VersionInfo.ProductVersion }
    $curText = '入っていない'; if ($cur) { $curText = 'v' + $cur }
    if ($cur -and $ver -and $cur -eq $ver) { return New-Row $id $name 'SAME' $curText ('v' + $ver) '' }

    # 起動中なら上書きできない
    $exePath = Join-Path $dest $exe
    $running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -ieq $exePath })
    if ($running.Count -gt 0) { return New-Row $id $name 'FAIL' $curText ('v' + $ver) 'アプリが起動しているため入れ替えられません。閉じてからもう一度実行してください。' }

    Initialize-WorkDir
    $zip = Join-Path $script:WorkDir ($folder + '.zip')
    Get-Download ([string]$App.url) $zip $name
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    if ($hash -ne ([string]$App.sha256).ToUpperInvariant()) {
        return New-Row $id $name 'FAIL' $curText ('v' + $ver) ('SHA-256 が目録と合いません（ダウンロードが壊れたか、ファイルが違います）。' + $hash)
    }

    Write-Status ($name + ' を展開しています')
    $tmpDir = Join-Path $script:WorkDir ($folder + '_new')
    if (Test-Path -LiteralPath $tmpDir) { Remove-Item -LiteralPath $tmpDir -Recurse -Force }
    Expand-Archive -LiteralPath $zip -DestinationPath $tmpDir -Force
    # ZIP の中がフォルダー 1 つだけなら、その中身を使う
    $src = $tmpDir
    $top = @(Get-ChildItem -LiteralPath $tmpDir -Force)
    if ($top.Count -eq 1 -and $top[0].PSIsContainer) { $src = $top[0].FullName }
    if (-not (Test-Path -LiteralPath (Join-Path $src $exe))) {
        return New-Row $id $name 'FAIL' $curText ('v' + $ver) ('ZIP の中に ' + $exe + ' がありません。')
    }

    if (-not (Test-Path -LiteralPath $Root)) { New-Item -ItemType Directory -Path $Root -Force | Out-Null }
    $old = $dest + '_old'
    if (Test-Path -LiteralPath $dest) {
        if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Recurse -Force }
        Move-Item -LiteralPath $dest -Destination $old -Force   # 前の版は _old に残す
    }
    Move-Item -LiteralPath $src -Destination $dest -Force
    if (Test-Path -LiteralPath $tmpDir) { Remove-Item -LiteralPath $tmpDir -Recurse -Force -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
    [System.IO.File]::WriteAllText($verFile, $ver, (New-Object System.Text.UTF8Encoding $false))
    $note = ''
    if (Test-Path -LiteralPath $old) { $note = '前の版は ' + (Split-Path -Leaf $old) + ' に残しています。' }
    if ($App.shortcut -eq $true) {
        try { New-Shortcut (Join-Path $dest $exe) $name; $note += 'デスクトップ（全員）にショートカットを作りました。' } catch { $note += 'ショートカットは作れませんでした。' }
    }
    return New-Row $id $name 'DONE' $curText ('v' + $ver) $note
}

function Install-OwnApps {
    param($Prof, [string]$ProfileDir)
    $source = [string](Get-Pref $Prof 'ownAppsManifest' '')
    if (-not $source) { return New-Row 'ownapps' '独自アプリ' 'FAIL' '' '' 'プロファイルに ownAppsManifest（目録の URL かパス）がありません。' }
    if ($source -notmatch '^https?://' -and -not [System.IO.Path]::IsPathRooted($source)) { $source = Join-Path $ProfileDir $source }
    Set-DeployStep 1 '目録の入手'
    try {
        $m = Get-AppsManifest $source
    } catch {
        return New-Row 'ownapps' '独自アプリ' 'FAIL' '' '' ('目録を読めませんでした：' + $_.Exception.Message)
    }
    $root = [string](Get-Pref $Prof 'ownAppsRoot' (Join-Path $env:ProgramFiles 'Org_Apps'))
    $rows = @()
    $i = 0
    $list = @($m.apps)
    foreach ($a in $list) {
        $i++
        Set-DeployStep ($i + 1) ([string]$a.name) ($list.Count + 1)
        if ([string]$a.exe -ieq 'ManabitoClassSetup.exe') {
            # このアプリ自身は、ここでは入れ替えない（起動中の自分は入れ替えられないため。起動時の更新で行う）
            $rows += New-Row ('ownapp' + $i) ([string]$a.name) 'SKIP' '' '' 'このアプリ自身は、起動したときの更新の確認で入れ替えます。'
            continue
        }
        if (Test-Cancelled) { $rows += New-Row ('ownapp' + $i) ([string]$a.name) 'SKIP' '' '' '取り消されたため行いませんでした。'; continue }
        try {
            $rows += Install-OneApp $a $root $i
        } catch {
            $rows += New-Row ('ownapp' + $i) ([string]$a.name) 'FAIL' '' '' ('途中で問題が起きました：' + $_.Exception.Message)
        }
    }
    if ($rows.Count -eq 0) { return New-Row 'ownapps' '独自アプリ' 'SKIP' '' '' '目録にアプリがありません。' }
    return $rows
}

# ---------------------------------------------------------------- まとめて（deploy.ps1・oneclick.ps1 から使う）

# 作業の一覧を画面用に登録し、行う順（削除が先、入れるのが後）の手順の並びを返す。
# -Office・-Adobe：0=何もしない 1=入れる 2=削除  -OwnApps：0/1
function Register-DeployPlan {
    param([int]$Office, [int]$Adobe, [int]$OwnApps)
    $steps = @()
    if ($Office -eq 2)  { Register-DeployItem 'office-remove' 'Microsoft 365（Office）の削除' 4;          $steps += ,@('Remove-Office',   'Microsoft 365（Office）の削除', 'office-remove') }
    if ($Adobe -eq 2)   { Register-DeployItem 'adobe-remove'  'Adobe Creative Cloud の削除' 5;            $steps += ,@('Remove-AdobeCC',  'Adobe Creative Cloud の削除', 'adobe-remove') }
    if ($Office -eq 1)  { Register-DeployItem 'office'        'Microsoft 365（Office）のインストール' 4;  $steps += ,@('Install-Office',  'Microsoft 365（Office）', 'office') }
    if ($Adobe -eq 1)   { Register-DeployItem 'adobe'         'Adobe Creative Cloud のインストール' 4;    $steps += ,@('Install-AdobeCC', 'Adobe Creative Cloud デスクトップアプリ', 'adobe') }
    if ($OwnApps -eq 1) { Register-DeployItem 'ownapps'       '独自アプリの入手と展開' 2;                 $steps += ,@('Install-OwnApps', '独自アプリ', 'ownapps') }
    return ,$steps
}

# 手順の並びを順に行い、結果の行を $Rows に足す。作業ごとの行（画面）も更新する。
function Invoke-DeployPlan {
    param($Steps, $Prof, [string]$ProfileDir, [System.Collections.Generic.List[object]]$Rows)
    foreach ($s in $Steps) {
        $func = $s[0]; $label = $s[1]; $itemId = $s[2]
        Enter-DeployItem $itemId
        if (Test-Cancelled) {
            [void]$Rows.Add((New-Row ($func.ToLower()) $label 'SKIP' '' '' '取り消されたため行いませんでした。'))
            Exit-DeployItem 'skip' '取り消されたため行いませんでした'
            continue
        }
        try {
            $rs = @(& $func $Prof $ProfileDir)
        } catch {
            $rs = @(New-Row ($func.ToLower()) $label 'FAIL' '' '' ('途中で問題が起きました：' + $_.Exception.Message))
        }
        $fails = @(); $dones = 0; $sames = 0; $n = 0
        foreach ($r in $rs) {
            if ($null -eq $r -or -not $r.PSObject.Properties['State']) { continue }
            [void]$Rows.Add($r); $n++
            Write-Host ('[{0}] {1} ／ {2} → {3} ／ {4}' -f $r.State, $r.Name, $r.Current, $r.Expected, $r.Detail)
            if ($r.State -eq 'FAIL') { $fails += $r }
            elseif ($r.State -eq 'DONE') { $dones++ }
            elseif ($r.State -eq 'SAME') { $sames++ }
        }
        if ($fails.Count -gt 0) { Exit-DeployItem 'fail' ($fails[0].Detail) }
        elseif ($dones -gt 0) {
            if ($n -gt 1) { Exit-DeployItem 'done' ('完了しました（入れた {0}・そのまま {1}）' -f $dones, $sames) }
            else { Exit-DeployItem 'done' (('完了しました　' + $rs[0].Detail).Trim()) }
        }
        elseif ($sames -gt 0) { Exit-DeployItem 'same' 'すでにそろっていたので、何もしませんでした' }
        else { Exit-DeployItem 'skip' '行いませんでした' }
    }
}

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

# ---------------------------------------------------------------- LAN の配布サーバーから Office のインストール元を受け取る
# 配布サーバー（src\share.cpp）の「OFFICE」の受け渡し。合言葉は使わない（夜間の自動実行のため）。
# 守り：setup.exe は毎回マイクロソフトから入手して署名を確かめる。LAN から受け取るのは Office\ の下のデータだけで、
#       .cab はすべてマイクロソフトの署名を確かめる（.dat は Office 自身が、署名済みの .cab の値で確かめる）。

$script:LanUdpPort = 47811

function Get-LanBroadcastTargets {
    $t = New-Object System.Collections.Generic.List[System.Net.IPAddress]
    $t.Add([System.Net.IPAddress]::Broadcast)
    foreach ($nic in [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
        if ($nic.OperationalStatus -ne 'Up' -or $nic.NetworkInterfaceType -eq 'Loopback') { continue }
        foreach ($u in $nic.GetIPProperties().UnicastAddresses) {
            if ($u.Address.AddressFamily -ne 'InterNetwork' -or -not $u.IPv4Mask) { continue }
            $a = $u.Address.GetAddressBytes(); $m = $u.IPv4Mask.GetAddressBytes()
            if ($m[0] -eq 0) { continue }
            $b = New-Object byte[] 4
            for ($i = 0; $i -lt 4; $i++) { $b[$i] = [byte](($a[$i] -band $m[$i]) -bor (255 -bxor $m[$i])) }
            $t.Add((New-Object System.Net.IPAddress (,$b)))
        }
    }
    return $t
}

# 戻り値：いちばん新しい版のインストール元を持つ配布サーバー（無ければ $null）。Tag は office ／ adobe
function Find-LanPackageServer {
    param([string]$Tag, [int]$Seconds = 3)
    $udp = $null
    try {
        $udp = New-Object System.Net.Sockets.UdpClient 0
        $udp.EnableBroadcast = $true
        $udp.Client.ReceiveTimeout = 500
        $q = [Text.Encoding]::ASCII.GetBytes('MNBSHARE?1')
        foreach ($t in Get-LanBroadcastTargets) {
            try { [void]$udp.Send($q, $q.Length, (New-Object System.Net.IPEndPoint($t, $script:LanUdpPort))) } catch { }
        }
        $best = $null; $bestVer = $null
        $until = (Get-Date).AddSeconds($Seconds)
        while ((Get-Date) -lt $until) {
            $ep = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
            try { $data = $udp.Receive([ref]$ep) } catch { continue }
            $msg = [Text.Encoding]::UTF8.GetString($data)
            if (-not $msg.StartsWith('MNBSHARE!1|')) { continue }
            $f = $msg.Split('|')
            # 6 番目から「名前=版」「バイト」の組が続く（office=…|…|adobe=…|…）
            $ver = $null; $bytes = [double]0
            for ($j = 6; $j + 1 -lt $f.Count; $j += 2) {
                if ($f[$j].StartsWith($Tag + '=')) { $ver = $f[$j].Substring($Tag.Length + 1); $bytes = [double]$f[$j + 1] }
            }
            if (-not $ver -or $ver -notmatch '^\d+(\.\d+)+$') { continue }
            $cand = [pscustomobject]@{ Name = $f[1]; Ip = $ep.Address.ToString(); Port = [int]$f[2]; Version = $ver; Bytes = $bytes }
            $cv = [version](($cand.Version -split '\.')[0..([Math]::Min(3, ($cand.Version -split '\.').Count - 1))] -join '.')
            if (-not $best -or $cv -gt $bestVer) { $best = $cand; $bestVer = $cv }
        }
        return $best
    } catch {
        Write-Host ('（LAN の配布サーバーを探せませんでした：' + $_.Exception.Message + '）')
        return $null
    } finally { if ($udp) { $udp.Close() } }
}

function Send-NetLine {
    param($Stream, [string]$Text)
    $b = [Text.Encoding]::UTF8.GetBytes($Text + "`n")
    $Stream.Write($b, 0, $b.Length)
}

function Read-NetLine {
    param($Stream)
    $ms = New-Object System.IO.MemoryStream
    while ($true) {
        $c = $Stream.ReadByte()
        if ($c -lt 0) { throw '配布サーバーとのつながりが切れました。' }
        if ($c -eq 10) { break }
        if ($ms.Length -gt 65536) { throw '配布サーバーの返事が長すぎます。' }
        $ms.WriteByte([byte]$c)
    }
    return ([Text.Encoding]::UTF8.GetString($ms.ToArray())).TrimEnd("`r")
}

# 受け取ってよい名前か（Prefix\ の下だけ。「..」・ドライブ名・使えない文字は受け取らない）
function Test-LanRelPath {
    param([string]$Rel, [string]$Prefix = 'Office')
    if (-not $Rel -or $Rel.Length -gt 300) { return $false }
    if (-not $Rel.StartsWith($Prefix + '\')) { return $false }
    if ($Rel -match '[:*?"<>|/]' -or $Rel -match '[\x00-\x1f]') { return $false }
    foreach ($seg in $Rel.Split('\')) { if (-not $seg -or $seg -eq '.' -or $seg -eq '..' -or $seg.EndsWith('.') -or $seg.EndsWith(' ')) { return $false } }
    return $true
}

# インストール元を Dest に受け取る。うまくいけば空、だめなら理由
function Receive-LanPackage {
    param($Server, [string]$Dest, [string]$ListCmd = 'OFFICE', [string]$GetCmd = 'OGET', [string]$Prefix = 'Office')
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $iar = $client.BeginConnect($Server.Ip, $Server.Port, $null, $null)
        if (-not $iar.AsyncWaitHandle.WaitOne(8000)) { return '配布サーバーにつながりませんでした。' }
        $client.EndConnect($iar)
        $ns = $client.GetStream()
        $ns.ReadTimeout = 120000
        Send-NetLine $ns ($ListCmd + '|' + $env:COMPUTERNAME)
        $head = Read-NetLine $ns
        if (-not $head.StartsWith('COUNT|')) { return ('配布サーバーの返事：' + $head) }
        $h = $head.Split('|')
        $n = [int]$h[1]; $total = [double]$h[2]
        $items = @()
        for ($i = 0; $i -lt $n; $i++) {
            $g = (Read-NetLine $ns).Split('|')
            $items += [pscustomobject]@{ Dir = ($g[0] -eq 'D'); Rel = $g[1]; Size = [double]$g[2] }
        }
        if (Test-Path -LiteralPath $Dest) { Remove-Item -LiteralPath $Dest -Recurse -Force }
        New-Item -ItemType Directory -Path $Dest -Force | Out-Null
        $buf = New-Object byte[] (1MB)
        $got = [double]0; $sw = [System.Diagnostics.Stopwatch]::StartNew(); $failed = 0; $ok = 0; $lastShow = [long]-1000
        for ($i = 0; $i -lt $items.Count; $i++) {
            $it = $items[$i]
            if (-not (Test-LanRelPath $it.Rel $Prefix)) { if (-not $it.Dir) { $failed++ }; continue }
            $path = Join-Path $Dest $it.Rel
            if ($it.Dir) { New-Item -ItemType Directory -Path $path -Force | Out-Null; continue }
            if (Test-Cancelled) { return '取り消されました。' }
            New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
            Send-NetLine $ns ($GetCmd + '|' + $i)
            $line = Read-NetLine $ns
            if (-not $line.StartsWith('DATA|')) { return ('配布サーバーが「' + $it.Rel + '」を渡せませんでした：' + $line) }
            $left = [double]$line.Substring(5)
            $fs = [System.IO.File]::Create($path)
            try {
                while ($left -gt 0) {
                    $want = [int][Math]::Min([double]$buf.Length, $left)
                    $k = $ns.Read($buf, 0, $want)
                    if ($k -le 0) { throw '配布サーバーとのつながりが切れました。' }
                    $fs.Write($buf, 0, $k)
                    $left -= $k; $got += $k
                    if ($sw.ElapsedMilliseconds - $lastShow -ge 1000 -or $left -eq 0) {   # 1 秒ごとに表示
                        $lastShow = $sw.ElapsedMilliseconds
                        $sec = [Math]::Max([double]0.001, $sw.Elapsed.TotalSeconds)
                        $rate = $got / $sec
                        $pct = -1; if ($total -gt 0) { $pct = [Math]::Min([double]99, [double]($got * 100 / $total)) }
                        $text = ('{0} から受信 {1} / {2}・{3}/秒' -f $Server.Name, (Format-Bytes $got), (Format-Bytes $total), (Format-Bytes $rate))
                        if ($rate -gt 0 -and $total -gt $got) { $text += ('・あと約 ' + (Format-Duration (($total - $got) / $rate))) }
                        Set-DeployProgress $pct $text
                    }
                }
            } finally { $fs.Close() }
            $ok++
        }
        try { Send-NetLine $ns ('DONE|' + $ok + '|' + $failed) } catch { }
        if ($failed -gt 0) { return ('受け取れないファイルが ' + $failed + ' 個ありました。') }
        $script:LastLanSummary = ('LAN の配布サーバー（{0}）から {1} を {2} で受け取りました' -f $Server.Name, (Format-Bytes $got), (Format-Duration $sw.Elapsed.TotalSeconds))
        return ''
    } catch {
        return ('受け取りの途中で問題が起きました：' + $_.Exception.Message)
    } finally { $client.Close() }
}

# 受け取ったインストール元の .cab がすべてマイクロソフトの署名つきか。だめなら理由
function Test-LanOfficeSource {
    param([string]$Dir)
    $cabs = @(Get-ChildItem -LiteralPath $Dir -Recurse -File -Filter '*.cab' -ErrorAction SilentlyContinue)
    if ($cabs.Count -eq 0) { return '受け取ったものの中に .cab がありません。' }
    foreach ($c in $cabs) {
        $bad = Test-Signer $c.FullName 'Microsoft Corporation'
        if ($bad) { return ($c.Name + '：' + $bad) }
    }
    return ''
}

# プロファイルが使うファイル（office.xml など）：プロファイルと同じフォルダーに無ければ、アプリの profiles から使う
# （プロファイルの設定画面で作ったプロファイルは %ProgramData%\ManabitoClassSetup\profiles に置かれ、office.xml を持たないため）
function Resolve-ProfileFile {
    param([string]$ProfileDir, [string]$Name)
    $p = Join-Path $ProfileDir $Name
    if (Test-Path -LiteralPath $p) { return $p }
    $appProfiles = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'profiles'
    return (Join-Path $appProfiles $Name)
}

function Install-Office {
    param($Prof, [string]$ProfileDir)
    $name = 'Microsoft 365（Office）'
    $ver = Get-RegValue 'HKLM:\SOFTWARE\Microsoft\Office\ClickToRun\Configuration' 'VersionToReport'
    if ($ver) { return New-Row 'office' $name 'SAME' ('入っている（' + $ver + '）') '入っている' '' }

    $url = [string](Get-Pref $Prof 'officeSetupUrl' 'https://officecdn.microsoft.com/pr/wsus/setup.exe')
    $xmlName = [string](Get-Pref $Prof 'officeConfig' 'office.xml')
    $xml = Resolve-ProfileFile $ProfileDir $xmlName
    if (-not (Test-Path -LiteralPath $xml)) { return New-Row 'office' $name 'FAIL' '入っていない' '入っている' ('構成ファイルが見つかりません：' + $xml) }

    Initialize-WorkDir
    $setup = Join-Path $script:WorkDir 'office-setup.exe'
    Set-DeployStep 1 '展開ツールのダウンロード'
    Get-Download $url $setup 'Office の展開ツール'
    Set-DeployStep 2 '電子署名の確認'
    $bad = Test-Signer $setup 'Microsoft Corporation'
    if ($bad) { return New-Row 'office' $name 'FAIL' '入っていない' '入っている' ('展開ツールを確かめられませんでした：' + $bad) }

    # LAN の配布サーバーにインストール元があれば、そこから受け取る（無ければインターネットから）
    $config = $xml
    $local = $null
    $how = 'インターネットから'
    $script:LastLanSummary = ''
    if ([bool](Get-Pref $Prof 'officeLanSource' $true)) {
        Set-DeployStep 3 'LAN の配布サーバーを探す'
        $server = Find-LanPackageServer 'office' 3
        if ($server) {
            Set-DeployStep 4 ('インストール元を受け取る（' + $server.Name + '、' + $server.Version + '）')
            $local = Join-Path $script:WorkDir 'office_source'
            $why = Receive-LanPackage $server $local 'OFFICE' 'OGET' 'Office'
            if (-not $why) { $why = Test-LanOfficeSource $local }
            if ($why) {
                Write-Host ('LAN から受け取れなかったため、インターネットから入れます：' + $why)
                if (Test-Path -LiteralPath $local) { Remove-Item -LiteralPath $local -Recurse -Force -ErrorAction SilentlyContinue }
                $local = $null
                $how = 'インターネットから（LAN からは受け取れませんでした：' + $why + '）'
            } else {
                [xml]$cfg = Get-Content -LiteralPath $xml -Raw -Encoding UTF8
                $cfg.Configuration.Add.SetAttribute('SourcePath', $local)
                $cfg.Configuration.Add.SetAttribute('AllowCdnFallback', 'TRUE')
                $config = Join-Path $script:WorkDir 'office-lan.xml'
                $cfg.Save($config)
                $how = $script:LastLanSummary
            }
        } else {
            Set-DeployStep 4 'LAN に配布サーバーが無いため、インターネットから入れます'
        }
    }

    Set-DeployStep 5 'インストール'
    $expect = [double](Get-Pref $Prof 'officeExpectedMB' 3500) * 1MB
    if ($local) { $expect = 0 }   # 手元から入れるので、受信量では進み具合を見ない
    $code = Start-ProcessWithProgress $setup @('/configure', ('"' + $config + '"')) $script:WorkDir 'Office をインストール中' $expect
    Set-DeployStep 6 '入ったことの確認'
    if ($local -and (Test-Path -LiteralPath $local)) { Remove-Item -LiteralPath $local -Recurse -Force -ErrorAction SilentlyContinue }  # 3.4 GB を残さない
    $after = Get-RegValue 'HKLM:\SOFTWARE\Microsoft\Office\ClickToRun\Configuration' 'VersionToReport'
    if ($code -ne 0 -or -not $after) {
        return New-Row 'office' $name 'FAIL' '入っていない' '入っている' ('展開ツールの終了コード ' + $code + '。' + $script:LastProcessSummary + '。記録は %TEMP% と C:\Windows\Temp の Office のログを見てください。')
    }
    return New-Row 'office' $name 'DONE' '入っていない' ('入っている（' + $after + '）') ('構成：' + $xmlName + '。' + $how + '。' + $script:LastProcessSummary)
}

# ---------------------------------------------------------------- Adobe

function Get-AdobeCCPath {
    foreach ($p in @((Join-Path $env:ProgramFiles 'Adobe\Adobe Creative Cloud\ACC\Creative Cloud.exe'),
                     (Join-Path ${env:ProgramFiles(x86)} 'Adobe\Adobe Creative Cloud\ACC\Creative Cloud.exe'))) {
        if ($p -and (Test-Path -LiteralPath $p)) { return $p }
    }
    return $null
}

# 受け取った Creative Cloud の ZIP を展開し、中の .exe と .dll がすべて Adobe の署名つきかを確かめる。Set-up.exe の場所（だめなら $null と理由）
function Expand-LanAdobe {
    param([string]$Dir, [string]$Dest)
    $zip = Get-ChildItem -LiteralPath $Dir -Recurse -File -Filter 'ACCCx*.zip' | Select-Object -First 1
    if (-not $zip) { return @($null, '受け取ったものの中に ZIP がありません。') }
    if (Test-Path -LiteralPath $Dest) { Remove-Item -LiteralPath $Dest -Recurse -Force }
    Expand-Archive -LiteralPath $zip.FullName -DestinationPath $Dest -Force
    $setup = Get-ChildItem -LiteralPath $Dest -Recurse -File -Filter 'Set-up.exe' | Select-Object -First 1
    if (-not $setup) { return @($null, 'ZIP の中に Set-up.exe がありません。') }
    $pe = @(Get-ChildItem -LiteralPath $Dest -Recurse -File -Include '*.exe', '*.dll')
    Set-DeployProgress -1 ('受け取ったもの {0} 個の電子署名を確かめています' -f $pe.Count)
    foreach ($f in $pe) {
        $bad = Test-Signer $f.FullName 'Adobe'
        if ($bad) { return @($null, ($f.Name + '：' + $bad)) }
    }
    return @($setup.FullName, '')
}

function Install-AdobeCC {
    param($Prof, [string]$ProfileDir)
    $name = 'Adobe Creative Cloud デスクトップアプリ'
    if (Get-AdobeCCPath) { return New-Row 'adobe' $name 'SAME' '入っている' '入っている' '' }
    Initialize-WorkDir
    $lanDir = Join-Path $script:WorkDir 'adobe_source'
    $lanSetupDir = Join-Path $script:WorkDir 'adobe_setup'
    $setup = $null
    $script:AdobeHow = 'インターネットから'
    try {
        # 1. LAN の配布サーバーにランチャーがあれば受け取る
        if ([bool](Get-Pref $Prof 'adobeLanSource' $true)) {
            Set-DeployStep 1 'LAN の配布サーバーを探す'
            $server = Find-LanPackageServer 'adobe' 3
            if ($server) {
                Set-DeployStep 2 ('インストーラーを受け取る（' + $server.Name + '、' + $server.Version + '）')
                $why = Receive-LanPackage $server $lanDir 'ADOBE' 'AGET' 'Adobe'
                if (-not $why) {
                    $r = Expand-LanAdobe $lanDir $lanSetupDir
                    $setup = $r[0]; $why = $r[1]
                }
                if ($why) {
                    Write-Host ('LAN から受け取れなかったため、インターネットから入れます：' + $why)
                    $setup = $null
                    $script:AdobeHow = 'インターネットから（LAN からは受け取れませんでした：' + $why + '）'
                } else {
                    $script:AdobeHow = $script:LastLanSummary
                }
            }
        }
        # 2. 無ければ、今までどおり小さなインストーラーをインターネットから
        if (-not $setup) {
            $url = [string](Get-Pref $Prof 'adobeInstallerUrl' '')
            if (-not $url) { return New-Row 'adobe' $name 'FAIL' '入っていない' '入っている' 'プロファイルに adobeInstallerUrl がありません。' }
            Set-DeployStep 2 'インストーラーのダウンロード'
            $setup = Join-Path $script:WorkDir 'Creative_Cloud_Set-Up.exe'
            Get-Download $url $setup 'Creative Cloud のインストーラー'
        }
        Set-DeployStep 3 '電子署名の確認'
        $bad = Test-Signer $setup 'Adobe'
        if ($bad) { return New-Row 'adobe' $name 'FAIL' '入っていない' '入っている' ('インストーラーを確かめられませんでした：' + $bad) }
        $row = Install-AdobeCCWith $Prof $setup
        if ($row.State -eq 'DONE') { $row.Detail = $script:AdobeHow + '。' + $row.Detail }
        return $row
    } finally {
        foreach ($d in @($lanDir, $lanSetupDir)) { if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force -ErrorAction SilentlyContinue } }
    }
}

# インストーラー（$Setup）を動かして、入るのを待つ
function Install-AdobeCCWith {
    param($Prof, [string]$Setup)
    $name = 'Adobe Creative Cloud デスクトップアプリ'
    $setup = $Setup
    Set-DeployStep 4 'インストール'

    $expect = [double](Get-Pref $Prof 'adobeExpectedMB' 600) * 1MB
    # 個人向けのインストーラーは、--silent でもサインイン画面を出して待つことがある（Adobe コミュニティの報告）。
    # 画面は隠さず、入ったと分かったらサインイン画面を閉じて先へ進む。15 分進まなければ打ち切る。
    $code = Start-ProcessWithProgress $setup @('--silent') (Split-Path -Parent $setup) 'Creative Cloud デスクトップアプリをインストール中' $expect `
            -Visible -DoneCheck { [bool](Get-AdobeCCPath) } -DoneGraceSeconds 60 -StallMinutes 15 -StallBytes 10MB
    if ($script:ProcessEndedEarly) {
        Stop-Process -Name 'Creative Cloud' -Force -ErrorAction SilentlyContinue   # 入ったあとに出るサインイン画面
    }
    if ($script:ProcessStalled) {
        return New-Row 'adobe' $name 'FAIL' '入っていない' '入っている' ('15 分たっても進まないため打ち切りました。インストーラーがサインインなどの画面の操作を待っていた可能性があります。人がいるときに［今すぐ実行］で試すか、Adobe の管理コンソールで作ったパッケージを使ってください。' + $script:LastProcessSummary)
    }
    # インストーラーが先に終わって、裏で続きを入れることがあるので、最大 15 分待つ
    Set-DeployStep 5 '仕上げと確認'
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
    $lnk.IconLocation = $Target + ',0'
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
    if ($cur -and $ver -and $cur -eq $ver) {
        try { New-ToolsShortcut (Join-Path $dest $exe) $name } catch { }   # 前の版で入れたものも「まなびとツール」にそろえる
        return New-Row $id $name 'SAME' $curText ('v' + $ver) ''
    }

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
    try { New-ToolsShortcut (Join-Path $dest $exe) $name; $note += 'スタートメニューの「まなびとツール」に入れました。' } catch { $note += 'スタートメニューに入れられませんでした。' }
    if ($App.shortcut -eq $true) {
        try { New-Shortcut (Join-Path $dest $exe) $name; $note += 'デスクトップ（全員）にショートカットを作りました。' } catch { $note += 'ショートカットは作れませんでした。' }
    }
    return New-Row $id $name 'DONE' $curText ('v' + $ver) $note
}

$script:OnlyAppsFilter = ''   # 独自アプリのタブから、選んだアプリの名前（| 区切り）

# スタートメニュー（全員）の「まなびとツール」グループにショートカットを作る
function New-ToolsShortcut {
    param([string]$Target, [string]$Name)
    $dir = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'まなびとツール'
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $ws = New-Object -ComObject WScript.Shell
    $lnk = $ws.CreateShortcut((Join-Path $dir ($Name + '.lnk')))
    $lnk.TargetPath = $Target
    $lnk.IconLocation = $Target + ',0'
    $lnk.WorkingDirectory = Split-Path -Parent $Target
    $lnk.Save()
    # スタートメニューの直下に同じ名前のショートカット（前の作り方）があれば消す
    $old = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) ($Name + '.lnk')
    if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Force -ErrorAction SilentlyContinue }
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
    if ($script:OnlyAppsFilter) {   # 独自アプリのタブで選んだものだけ
        $want = @($script:OnlyAppsFilter.Split('|') | Where-Object { $_ })
        $list = @($list | Where-Object { $want -contains [string]$_.name })
    }
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
    param([int]$Office, [int]$Adobe, [int]$OwnApps, [int]$ExtraCount = 0)
    $steps = @()
    if ($Office -eq 2)  { Register-DeployItem 'office-remove' 'Microsoft 365（Office）の削除' 4;          $steps += ,@('Remove-Office',   'Microsoft 365（Office）の削除', 'office-remove') }
    if ($Adobe -eq 2)   { Register-DeployItem 'adobe-remove'  'Adobe Creative Cloud の削除' 5;            $steps += ,@('Remove-AdobeCC',  'Adobe Creative Cloud の削除', 'adobe-remove') }
    if ($Office -eq 1)  { Register-DeployItem 'office'        'Microsoft 365（Office）のインストール' 6;  $steps += ,@('Install-Office',  'Microsoft 365（Office）', 'office') }
    if ($Adobe -eq 1)   { Register-DeployItem 'adobe'         'Adobe Creative Cloud のインストール' 5;    $steps += ,@('Install-AdobeCC', 'Adobe Creative Cloud デスクトップアプリ', 'adobe') }
    if ($ExtraCount -gt 0) { Register-DeployItem 'extra'   'この講座で入れるアプリ' $ExtraCount; $steps += ,@('Install-ExtraApps', 'この講座で入れるアプリ', 'extra') }
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

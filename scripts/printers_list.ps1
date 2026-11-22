# まなびと講習準備 ― 先生の PC のプリンターの一覧（先生用の窓の［プリンター］タブ。管理者は要らない）
#
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File printers_list.ps1 -OutFile <結果.txt>
# 結果（1 行 1 台）：printer|名前|ホスト|方式（raw・ipp）|ポート番号|ドライバー|既定（1/0）|配れる（1/0）|配れない理由|IPP のパス
# 配れるのは、IP アドレス（ホスト名）でつないだネットワークのプリンターだけ。USB・WSD・Windows に最初から入っているものは配れない。

param([Parameter(Mandatory = $true)][string]$OutFile)

$out = New-Object System.Collections.Generic.List[string]
function Clean { param($v) if ($null -eq $v) { return '' }; return ([string]$v) -replace '[|\r\n]+', ' ' }
try {
    $def = ''
    try { $def = [string](Get-CimInstance -ClassName Win32_Printer -Filter 'Default=TRUE' -ErrorAction Stop | Select-Object -First 1).Name } catch { }
    $builtin = @('Microsoft Print to PDF', 'Microsoft XPS Document Writer', 'OneNote', 'OneNote (Desktop)', 'OneNote for Windows 10', 'Fax')
    foreach ($p in @(Get-Printer -ErrorAction Stop | Sort-Object Name)) {
        $hostName = ''; $proto = ''; $port = 0; $ok = 0; $why = ''; $ippPath = ''
        $isDef = 0; if ($p.Name -eq $def) { $isDef = 1 }
        if ($builtin -contains $p.Name -or $p.Name -like 'OneNote*') {
            $why = 'Windows に最初から入っています'
        } elseif ([string]$p.PortName -match '^https?://([^/:]+)(?::(\d+))?(/[A-Za-z0-9/_.\-]*)?') {
            $hostName = $Matches[1]; $proto = 'ipp'; $port = 631; if ($Matches[2]) { $port = [int]$Matches[2] }; $ok = 1
            $ippPath = '/ipp/print'; if ($Matches[3]) { $ippPath = $Matches[3] }
        } else {
            $pp = $null
            try { $pp = Get-PrinterPort -Name $p.PortName -ErrorAction Stop } catch { }
            if ($pp -and $pp.PrinterHostAddress) {
                $hostName = [string]$pp.PrinterHostAddress; $proto = 'raw'; $port = 9100
                if ($pp.PortNumber) { $port = [int]$pp.PortNumber }
                $ok = 1
            } elseif ([string]$p.PortName -like 'WSD*') {
                $why = 'WSD でつないでいるため配れません（IP アドレスで追加し直すと配れます）'
            } elseif ([string]$p.PortName -like 'USB*') {
                $why = 'USB でつないでいるため配れません'
            } else {
                $why = 'この PC だけのつなぎ方のため配れません（' + $p.PortName + '）'
            }
        }
        $out.Add(('printer|{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}' -f (Clean $p.Name), (Clean $hostName), $proto, $port, (Clean $p.DriverName), $isDef, $ok, (Clean $why), (Clean $ippPath)))
    }
} catch {
    $out.Clear()
    $out.Add('error|' + (Clean $_.Exception.Message))
}
[System.IO.File]::WriteAllLines($OutFile, [string[]]$out.ToArray(), (New-Object System.Text.UTF8Encoding $true))
exit 0

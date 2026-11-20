# まなびと講習準備 ― 配布サーバーで、Creative Cloud デスクトップアプリ（ランチャー）のインストーラーを一度だけ入手する（管理者は要らない）
#
# 配布用の窓の［Creative Cloud（ランチャー）を用意する］から呼ぶ:
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File adobe_source.ps1
#       -ProfilePath <プロファイル.json> -OutDir <置き場所> -StatusFile <状況.txt> [-LogFile <記録.txt>]
#
# Adobe の公式ページ「Download Creative Cloud desktop app using direct links」にある Windows（64 ビット）用の ZIP を使う。
# リンクは版ごとに変わるので、毎回ページを読んでいちばん新しい版を選ぶ（プロファイルの adobeOfflineZipUrl があればそれを使う）。
# 使うのは https://*.adobe.com/ の ACCCx…zip だけ。ZIP を展開して Set-up.exe の Adobe の署名を確かめてから置く。
# 状況.txt（1 行）：page ／ download|受信済みバイト|目安バイト ／ check ／ done|版|バイト ／ fail|理由

param(
    [string]$ProfilePath = '',
    [Parameter(Mandatory = $true)][string]$OutDir,
    [Parameter(Mandatory = $true)][string]$StatusFile,
    [string]$LogFile = ''
)

$ErrorActionPreference = 'Stop'
if ($LogFile) { try { Start-Transcript -LiteralPath $LogFile -Force | Out-Null } catch { } }
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'modules\common.ps1')
. (Join-Path $here 'modules\deploy.ps1')
$utf8 = New-Object System.Text.UTF8Encoding $false
function Set-SourceStatus { param([string]$Text) [System.IO.File]::WriteAllText($StatusFile, $Text, $utf8) }

# ページから Windows（64 ビット）用の ZIP のリンクを選ぶ（いちばん新しい版）
function Find-AdobeZipUrl {
    param([string]$Page)
    $ProgressPreference = 'SilentlyContinue'
    $html = (Invoke-WebRequest -Uri $Page -UseBasicParsing -TimeoutSec 60).Content
    $found = [regex]::Matches($html, 'https://ccmdls?\.adobe\.com/[^"''\s<>()]*?/win64/ACCCx([0-9_]+)\.zip')
    $best = $null; $bestVer = $null
    foreach ($m in $found) {
        try { $v = [version](($m.Groups[1].Value -replace '_', '.') -replace '^(\d+\.\d+\.\d+\.\d+)\..*$', '$1') } catch { continue }
        if (-not $best -or $v -gt $bestVer) { $best = $m.Value; $bestVer = $v }
    }
    return $best
}

# 1 秒ごとに状況を書きながらダウンロードする
function Save-WithStatus {
    param([string]$Url, [string]$Path)
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $req = [System.Net.HttpWebRequest]::Create($Url)
    $req.Timeout = 60000
    $res = $req.GetResponse()
    $total = [double]$res.ContentLength
    $in = $res.GetResponseStream()
    $out = [System.IO.File]::Create($Path)
    try {
        $buf = New-Object byte[] (1MB)
        $got = [double]0
        $sw = [System.Diagnostics.Stopwatch]::StartNew(); $last = [long]-1000
        while (($k = $in.Read($buf, 0, $buf.Length)) -gt 0) {
            $out.Write($buf, 0, $k)
            $got += $k
            if ($sw.ElapsedMilliseconds - $last -ge 1000) {
                $last = $sw.ElapsedMilliseconds
                Set-SourceStatus ('download|{0}|{1}' -f [long]$got, [long][Math]::Max([double]0, $total))
            }
        }
    } finally { $out.Close(); $in.Close(); $res.Close() }
}

try {
    $prof = $null
    if ($ProfilePath) { $prof = Get-Content -LiteralPath $ProfilePath -Raw -Encoding UTF8 | ConvertFrom-Json }
    Set-SourceStatus 'page'
    $url = [string](Get-Pref $prof 'adobeOfflineZipUrl' '')
    if (-not $url) {
        $page = [string](Get-Pref $prof 'adobeDirectLinksPage' 'https://helpx.adobe.com/download-install/apps/download-install-apps/creative-cloud-apps/download-creative-cloud-desktop-app-using-direct-links.html')
        $url = Find-AdobeZipUrl $page
        if (-not $url) { throw 'Adobe のページに、Windows（64 ビット）用の ZIP のリンクが見つかりませんでした。ページが変わった可能性があります。' }
    }
    $u = [Uri]$url
    if ($u.Scheme -ne 'https' -or -not ($u.Host -eq 'adobe.com' -or $u.Host.EndsWith('.adobe.com')) -or $u.AbsolutePath -notmatch '/ACCCx[0-9_]+\.zip$') {
        throw ('Adobe の ZIP のリンクではないため使いません：' + $url)
    }
    $name = [System.IO.Path]::GetFileName($u.AbsolutePath)
    Write-Host ('使うリンク：' + $url)

    $newDir = $OutDir.TrimEnd('\') + '.new'
    if (Test-Path -LiteralPath $newDir) { Remove-Item -LiteralPath $newDir -Recurse -Force }
    New-Item -ItemType Directory -Path (Join-Path $newDir 'Adobe') -Force | Out-Null
    $zip = Join-Path $newDir ('Adobe\' + $name)
    Save-WithStatus $url $zip

    # 展開して Set-up.exe の署名を確かめる（確かめたら展開したものは消す。配るのは ZIP のまま）
    Set-SourceStatus 'check'
    $tmp = Join-Path $env:TEMP ('ManabitoClassSetup\adobe_check_' + [guid]::NewGuid().ToString('N'))
    try {
        Expand-Archive -LiteralPath $zip -DestinationPath $tmp -Force
        $setup = Get-ChildItem -LiteralPath $tmp -Recurse -File -Filter 'Set-up.exe' | Select-Object -First 1
        if (-not $setup) { throw 'ZIP の中に Set-up.exe がありません。' }
        $bad = Test-Signer $setup.FullName 'Adobe'
        if ($bad) { throw ('Set-up.exe の署名を確かめられませんでした：' + $bad) }
    } finally { if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue } }

    if (Test-Path -LiteralPath $OutDir) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
    Move-Item -LiteralPath $newDir -Destination $OutDir
    $ver = ($name -replace '^ACCCx', '' -replace '\.zip$', '') -replace '_', '.'
    Set-SourceStatus ('done|{0}|{1}' -f $ver, (Get-Item -LiteralPath (Join-Path $OutDir ('Adobe\' + $name))).Length)
} catch {
    Set-SourceStatus ('fail|' + ($_.Exception.Message -replace '[|\r\n]+', ' '))
}
if ($LogFile) { try { Stop-Transcript | Out-Null } catch { } }
exit 0

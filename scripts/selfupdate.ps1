# まなびと講習準備 ― 新しい版に入れ替えて起動し直す（管理者として動く）
#
# 画面のアプリが、この部品を一時フォルダーに写してから「管理者として」呼ぶ（入れ替える scripts の中から動かないため）:
#   powershell.exe ... -File selfupdate.ps1 -Url <ZIP> -Sha256 <値> -Version <版> -Target <Program Files の場所>
#       -ParentPid <画面のアプリ> -StatusFile <状況.txt> -ResultFile <結果.txt> -LogFile <記録.txt>
#
# 状況.txt：download（ダウンロード中）→ ready（確かめ終わった。画面のアプリは閉じてよい）／ fail|理由
# 入れ替えは、画面のアプリが閉じてから行い、終わったら利用者の権限で起動し直す（エクスプローラー経由）。
# 結果.txt は、起動し直したアプリが最初に読んで表示する（%LOCALAPPDATA%\ManabitoClassSetup\update_result.txt）。

param(
    [Parameter(Mandatory = $true)][string]$Url,
    [Parameter(Mandatory = $true)][string]$Sha256,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Target,
    [int]$ParentPid = 0,
    [Parameter(Mandatory = $true)][string]$StatusFile,
    [Parameter(Mandatory = $true)][string]$ResultFile,
    [string]$LogFile = ''
)

$ErrorActionPreference = 'Stop'
if ($LogFile) { try { Start-Transcript -LiteralPath $LogFile -Force | Out-Null } catch { } }
$utf8 = New-Object System.Text.UTF8Encoding $true
function Set-UpdateStatus { param([string]$Text) [System.IO.File]::WriteAllText($StatusFile, $Text, $utf8); Write-Host ('[状況] ' + $Text) }
function Set-UpdateResult { param([string]$Text) [System.IO.File]::WriteAllText($ResultFile, $Text, $utf8); Write-Host ('[結果] ' + $Text) }
$exe = Join-Path $Target 'ManabitoClassSetup.exe'
$wasReady = $false   # 画面のアプリに「閉じてよい」と伝えたか（伝えたら、成否にかかわらず起動し直す）

try {
    Set-UpdateStatus 'download'
    $work = Join-Path $env:ProgramData ('ManabitoClassSetup\update_' + $Version)
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
    New-Item -ItemType Directory -Path $work -Force | Out-Null
    $zip = Join-Path $work 'update.zip'
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    if ($Url -match '^https?://') {
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing
    } else {
        Copy-Item -LiteralPath $Url -Destination $zip -Force   # 共有フォルダーなど
    }
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    if ($hash -ne $Sha256.ToUpperInvariant()) { throw ('SHA-256 が目録と合いません（' + $hash + '）。ダウンロードが壊れたか、ファイルが違います。') }
    $dir = Join-Path $work 'files'
    Expand-Archive -LiteralPath $zip -DestinationPath $dir -Force
    # ZIP の中から「ManabitoClassSetup.exe と scripts が並んでいるフォルダー」を探す
    $app = Get-ChildItem -LiteralPath $dir -Recurse -File -Filter 'ManabitoClassSetup.exe' |
           Where-Object { Test-Path -LiteralPath (Join-Path $_.DirectoryName 'scripts\install.ps1') } | Select-Object -First 1
    if (-not $app) { throw 'ZIP の中に、ManabitoClassSetup.exe と scripts が並んだフォルダーがありません。' }
    $src = $app.DirectoryName
    Set-UpdateStatus 'ready'
    $wasReady = $true

    # 画面のアプリが閉じるのを待つ（最大 2 分）
    if ($ParentPid) {
        try { Wait-Process -Id $ParentPid -Timeout 120 -ErrorAction Stop } catch { }
        Start-Sleep -Seconds 1
    }
    # 新しい版の install.ps1 で入れ替える
    $out = Join-Path $work 'install_result.txt'
    & (Join-Path $src 'scripts\install.ps1') -Source $src -Target $Target -OutFile $out
    $res = [System.IO.File]::ReadAllText($out)
    if (-not $res.StartsWith('OK')) { throw ('入れ替えられませんでした：' + $res) }
    Set-UpdateResult ('OK|' + $Version)
} catch {
    $msg = $_.Exception.Message -replace '[|\r\n]+', ' '
    Set-UpdateStatus ('fail|' + $msg)
    Set-UpdateResult ('NG|' + $msg)
}
# 画面のアプリを閉じさせたなら、入れ替えがうまくいかなくても、利用者の権限で起動し直す（エクスプローラー経由）。
# 起動し直したアプリが、結果ファイルを読んで「更新した／できなかった」を表示する。
if ($wasReady) {
    try { if (Test-Path -LiteralPath $exe) { Start-Process -FilePath 'explorer.exe' -ArgumentList ('"' + $exe + '"') } } catch { }
}
if ($LogFile) { try { Stop-Transcript | Out-Null } catch { } }
exit 0

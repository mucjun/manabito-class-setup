# まなびと講習準備 ― 配布サーバーで、Office のインストール元を一度だけダウンロードする（管理者は要らない）
#
# 配布用の窓の［Office のインストール元を用意する］から呼ぶ:
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File office_source.ps1
#       -ProfilePath <プロファイル.json> -OutDir <置き場所> -StatusFile <状況.txt> [-LogFile <記録.txt>]
#
# 展開ツール（setup.exe）の /download を使う（マイクロソフトが用意している使い方）。構成は講習用と同じ office.xml に SourcePath を足したもの。
# 新しいものを OutDir.new に作ってから入れ替えるので、途中で失敗しても前のインストール元は残る。
# 状況.txt（1 行）：tool（展開ツールの入手）／ download|受信済みバイト|目安バイト ／ done|版|バイト ／ fail|理由

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
$script:WorkDir = Join-Path $env:TEMP 'ManabitoClassSetup'
$utf8 = New-Object System.Text.UTF8Encoding $false
function Set-SourceStatus { param([string]$Text) [System.IO.File]::WriteAllText($StatusFile, $Text, $utf8) }

try {
    $prof = $null
    $profileDir = Join-Path (Split-Path -Parent $here) 'profiles'
    if ($ProfilePath) {
        $prof = Get-Content -LiteralPath $ProfilePath -Raw -Encoding UTF8 | ConvertFrom-Json
        $profileDir = Split-Path -Parent $ProfilePath
    }
    Set-SourceStatus 'tool'
    New-Item -ItemType Directory -Path $script:WorkDir -Force | Out-Null
    $setup = Join-Path $script:WorkDir 'office-setup.exe'
    Get-Download ([string](Get-Pref $prof 'officeSetupUrl' 'https://officecdn.microsoft.com/pr/wsus/setup.exe')) $setup 'Office の展開ツール'
    $bad = Test-Signer $setup 'Microsoft Corporation'
    if ($bad) { throw ('展開ツールを確かめられませんでした：' + $bad) }

    # 構成：講習用の office.xml に SourcePath（ダウンロード先）を足す
    $xmlPath = Join-Path $profileDir ([string](Get-Pref $prof 'officeConfig' 'office.xml'))
    [xml]$cfg = Get-Content -LiteralPath $xmlPath -Raw -Encoding UTF8
    $newDir = $OutDir.TrimEnd('\') + '.new'
    if (Test-Path -LiteralPath $newDir) { Remove-Item -LiteralPath $newDir -Recurse -Force }
    New-Item -ItemType Directory -Path $newDir -Force | Out-Null
    $cfg.Configuration.Add.SetAttribute('SourcePath', $newDir)
    $cfgFile = Join-Path $script:WorkDir 'office-download.xml'
    $cfg.Save($cfgFile)

    # ダウンロード（進み具合は、置き場所の大きさで見る）
    $expect = [double](Get-Pref $prof 'officeExpectedMB' 3500) * 1MB
    $p = Start-Process -FilePath $setup -ArgumentList @('/download', ('"' + $cfgFile + '"')) -WorkingDirectory $script:WorkDir -WindowStyle Hidden -PassThru
    while (-not $p.HasExited) {
        Start-Sleep -Seconds 2
        $size = [double](@(Get-ChildItem -LiteralPath $newDir -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum)
        Set-SourceStatus ('download|{0}|{1}' -f [long]$size, [long]$expect)
    }
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw ('展開ツールの終了コード ' + $p.ExitCode + '（ダウンロードできませんでした）') }

    $verDir = Get-ChildItem -LiteralPath (Join-Path $newDir 'Office\Data') -Directory -ErrorAction SilentlyContinue |
              Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $verDir) { throw 'ダウンロードしたものの中に、Office の版のフォルダーがありません。' }
    $total = [double](@(Get-ChildItem -LiteralPath $newDir -Recurse -File -Force | Measure-Object -Property Length -Sum).Sum)

    # 入れ替え
    if (Test-Path -LiteralPath $OutDir) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
    Move-Item -LiteralPath $newDir -Destination $OutDir
    Set-SourceStatus ('done|{0}|{1}' -f $verDir.Name, [long]$total)
} catch {
    Set-SourceStatus ('fail|' + ($_.Exception.Message -replace '[|\r\n]+', ' '))
}
if ($LogFile) { try { Stop-Transcript | Out-Null } catch { } }
exit 0

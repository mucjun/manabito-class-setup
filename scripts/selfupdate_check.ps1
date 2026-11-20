# まなびと講習準備 ― 新しい版があるかを確かめる（管理者は要らない）
#
# 画面のアプリが起動時に呼ぶ:
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File selfupdate_check.ps1
#       -ProfilePath <プロファイル.json> -CurrentVersion 0.12.0 -OutFile <結果.txt>
#
# 結果.txt（1 行）：none ／ newer|版|URL|SHA-256 ／ error|理由
# 目録（ownAppsManifest）の apps のうち、exe が ManabitoClassSetup.exe のものを、このアプリ自身として見る。

param(
    [string]$ProfilePath = '',
    [Parameter(Mandatory = $true)][string]$CurrentVersion,
    [Parameter(Mandatory = $true)][string]$OutFile
)

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'modules\common.ps1')
. (Join-Path $here 'modules\deploy.ps1')
# 管理者でなくても書ける場所で作業する
$script:WorkDir = Join-Path $env:TEMP 'ManabitoClassSetup'
# 起動時の確認なので、見つからないときは取り直さずにすぐ終える
$script:DownloadTries = 1

function Write-Answer {
    param([string]$Text)
    [System.IO.File]::WriteAllText($OutFile, $Text, (New-Object System.Text.UTF8Encoding $true))
    exit 0
}

try {
    if (-not $ProfilePath) { Write-Answer 'none' }
    $prof = Get-Content -LiteralPath $ProfilePath -Raw -Encoding UTF8 | ConvertFrom-Json
    $source = [string](Get-Pref $prof 'ownAppsManifest' '')
    if (-not $source) { Write-Answer 'none' }
    if ($source -notmatch '^https?://' -and -not [System.IO.Path]::IsPathRooted($source)) {
        $source = Join-Path (Split-Path -Parent $ProfilePath) $source
    }
    $m = Get-AppsManifest $source
    $self = @($m.apps) | Where-Object { [string]$_.exe -ieq 'ManabitoClassSetup.exe' } | Select-Object -First 1
    if (-not $self) { Write-Answer 'none' }
    $latest = [version]([string]$self.version)
    $mine = [version]$CurrentVersion
    if ($latest -le $mine) { Write-Answer 'none' }
    if (-not $self.url -or -not $self.sha256) { Write-Answer 'error|目録の「まなびと講習準備」に url か sha256 がありません。' }
    Write-Answer ('newer|{0}|{1}|{2}' -f $self.version, $self.url, $self.sha256)
} catch {
    Write-Answer ('error|' + ($_.Exception.Message -replace '[|\r\n]+', ' '))
}

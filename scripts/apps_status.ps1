# まなびと講習準備 ― 独自アプリのタブに出す一覧（管理者は要らない）
#
# 画面のアプリが「独自アプリ」タブを開いたとき・読み直すときに呼ぶ:
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File apps_status.ps1
#       -ProfilePath <プロファイル.json> -CurrentVersion 0.16.0 -OutFile <結果.txt>
#
# 結果.txt（1 行 1 アプリ）：app|名前|exe|入っている版|最新の版|状態|URL|SHA-256
#   状態：none（入っていない）／ old（新しい版あり）／ same（最新）／ newer-here（入っている版のほうが新しい）
#   目録を読めないとき：error|理由
# まなびと講習準備（このアプリ自身）は、入っている版＝今動いている版。

param(
    [string]$ProfilePath = '',
    [Parameter(Mandatory = $true)][string]$CurrentVersion,
    [Parameter(Mandatory = $true)][string]$OutFile
)

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'modules\common.ps1')
. (Join-Path $here 'modules\deploy.ps1')
$script:WorkDir = Join-Path $env:TEMP 'ManabitoClassSetup'
$script:DownloadTries = 1
$out = New-Object System.Collections.Generic.List[string]

function Clean { param($v) if ($null -eq $v) { return '' }; return ([string]$v) -replace '[|\r\n]+', ' ' }

function Compare-Ver {
    param([string]$A, [string]$B)   # A が新しければ 1、同じなら 0、古ければ -1、比べられなければ $null
    try {
        $va = [version](($A -split '[^\d.]')[0]); $vb = [version](($B -split '[^\d.]')[0])
        return $va.CompareTo($vb)
    } catch { return $null }
}

try {
    $prof = $null
    if ($ProfilePath) { $prof = Get-Content -LiteralPath $ProfilePath -Raw -Encoding UTF8 | ConvertFrom-Json }
    $source = [string](Get-Pref $prof 'ownAppsManifest' '')
    if (-not $source) { throw 'プロファイルに ownAppsManifest（目録の URL かパス）がありません。' }
    if ($source -notmatch '^https?://' -and -not [System.IO.Path]::IsPathRooted($source)) { $source = Join-Path (Split-Path -Parent $ProfilePath) $source }
    $m = Get-AppsManifest $source
    $pf = $env:ProgramFiles; if (-not $pf) { $pf = 'C:\Program Files' }
    $root = [string](Get-Pref $prof 'ownAppsRoot' (Join-Path $pf 'Org_Apps'))
    foreach ($a in @($m.apps)) {
        $name = [string]$a.name; $exe = [string]$a.exe; $latest = [string]$a.version
        if ($exe -ieq 'ManabitoClassSetup.exe') {
            $cur = $CurrentVersion
        } else {
            $dest = Join-Path $root ([string]$a.folder)
            $cur = ''
            $verFile = Join-Path $dest '.manabito_version'
            if (Test-Path -LiteralPath $verFile) { $cur = (Get-Content -LiteralPath $verFile -Raw -Encoding UTF8).Trim() }
            elseif (Test-Path -LiteralPath (Join-Path $dest $exe)) { $cur = (Get-Item -LiteralPath (Join-Path $dest $exe)).VersionInfo.ProductVersion }
        }
        $state = 'none'
        if ($cur) {
            $c = Compare-Ver $latest $cur
            if ($c -eq $null) { if ($cur -eq $latest) { $state = 'same' } else { $state = 'old' } }
            elseif ($c -gt 0) { $state = 'old' } elseif ($c -eq 0) { $state = 'same' } else { $state = 'newer-here' }
        }
        $out.Add(('app|{0}|{1}|{2}|{3}|{4}|{5}|{6}' -f (Clean $name), (Clean $exe), (Clean $cur), (Clean $latest), $state, (Clean $a.url), (Clean $a.sha256)))
    }
} catch {
    $out.Clear()
    $out.Add('error|' + ($_.Exception.Message -replace '[|\r\n]+', ' '))
}
[System.IO.File]::WriteAllLines($OutFile, [string[]]$out.ToArray(), (New-Object System.Text.UTF8Encoding $true))
exit 0

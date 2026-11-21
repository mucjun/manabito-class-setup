# まなびと講習準備 ― プロファイルの設定画面から、プロファイルを保存・削除する（管理者として動く）
#
#   powershell.exe ... -File save_profile.ps1 -Base <元にするプロファイル> -FileName <xxx.json> -OutFile <結果.txt>
#       -Name <名前> -Description <説明> -Office install|skip -Adobe install|skip -ExtraApps "id,id" -PdfDefault <id か空>
#       -WallpaperColor #RRGGBB -WindowsUpdate stop|check-only
#   削除（自分で作ったもの・変えたものを元に戻す）：-Delete -FileName <xxx.json> -OutFile <結果.txt>
#
# 置き場所：%ProgramData%\ManabitoClassSetup\profiles（アプリの更新で消えない。アプリの profiles と同じ名前なら、こちらが優先）
# 元のプロファイルを読み、画面で変えた項目だけを書き換えて保存する（ほかの設定はそのまま受け継ぐ）。
# 結果.txt：1 行目 OK か NG、2 行目に説明

param(
    [string]$Base = '',
    [Parameter(Mandatory = $true)][string]$FileName,
    [Parameter(Mandatory = $true)][string]$OutFile,
    [switch]$Delete,
    [string]$Name = '',
    [string]$Description = '',
    [string]$Office = 'install',
    [string]$Adobe = 'skip',
    [string]$ExtraApps = '',
    [string]$PdfDefault = '',
    [string]$WallpaperColor = '#1F3A5F',
    [string]$WindowsUpdate = 'stop'
)

$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding $true
function Write-Result { param([bool]$Ok, [string]$Text) [System.IO.File]::WriteAllLines($OutFile, [string[]]@($(if ($Ok) { 'OK' } else { 'NG' }), $Text), $utf8); exit 0 }

try {
    $here = Split-Path -Parent $MyInvocation.MyCommand.Path
    $appProfiles = Join-Path (Split-Path -Parent $here) 'profiles'
    $pd = $env:ProgramData; if (-not $pd) { $pd = 'C:\ProgramData' }
    $customDir = Join-Path $pd 'ManabitoClassSetup\profiles'
    if ($FileName -notmatch '^[A-Za-z0-9_\-]{1,40}\.json$') { Write-Result $false 'ファイル名の形が違います。' }
    $target = Join-Path $customDir $FileName

    if ($Delete) {
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
        Write-Result $true '削除しました（アプリに入っているプロファイルなら、元の内容に戻りました）。'
    }

    # 元にするプロファイル：アプリの profiles か、保存の場所の中のものだけ
    $baseFull = [System.IO.Path]::GetFullPath($Base)
    $okBase = $false
    foreach ($d in @($appProfiles, $customDir)) {
        $dd = [System.IO.Path]::GetFullPath($d).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
        if ($baseFull.StartsWith($dd, [StringComparison]::OrdinalIgnoreCase)) { $okBase = $true }
    }
    if (-not $okBase -or -not (Test-Path -LiteralPath $baseFull)) { Write-Result $false ('元にするプロファイルの場所が違います：' + $Base) }
    $prof = Get-Content -LiteralPath $baseFull -Raw -Encoding UTF8 | ConvertFrom-Json

    # 値を確かめる
    $Name = $Name.Trim()
    if (-not $Name -or $Name.Length -gt 40 -or $Name -match '[\x00-\x1f"\\]') { Write-Result $false '名前は 1〜40 文字で、「"」「\」を使わずに入れてください。' }
    if ($Description.Length -gt 200 -or $Description -match '[\x00-\x1f]') { Write-Result $false '説明は 200 文字までにしてください。' }
    if ($Office -notin @('install', 'skip')) { Write-Result $false 'Office の値が違います。' }
    if ($Adobe -notin @('install', 'skip')) { Write-Result $false 'Adobe の値が違います。' }
    if ($WallpaperColor -notmatch '^#[0-9A-Fa-f]{6}$') { Write-Result $false '背景色は #1F3A5F のような形で入れてください。' }
    if ($WindowsUpdate -notin @('stop', 'check-only')) { Write-Result $false 'Windows Update の値が違います。' }
    $catalog = @{}
    foreach ($line in @(Get-Content -LiteralPath (Join-Path $here 'extra_apps.txt') -Encoding UTF8)) {
        $l = ([string]$line).TrimStart([char]0xFEFF).Trim()
        if (-not $l -or $l.StartsWith('#')) { continue }
        $f = $l.Split('|')
        if ($f.Count -ge 6) { $catalog[$f[0]] = ($f[4] -eq '1') }
    }
    $ids = @($ExtraApps.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    foreach ($id in $ids) { if (-not $catalog.ContainsKey($id)) { Write-Result $false ('目録に無いアプリです：' + $id) } }
    if ($PdfDefault) {
        if (-not $catalog.ContainsKey($PdfDefault) -or -not $catalog[$PdfDefault]) { Write-Result $false 'PDF を開くアプリの値が違います。' }
        if ($ids -notcontains $PdfDefault) { Write-Result $false 'PDF を開くアプリは、「この講座で入れるアプリ」にも入れてください。' }
    }

    # 書き換え（無いキーは足す）
    $set = @{ name = $Name; description = $Description; office = $Office; adobe = $Adobe; extraApps = ($ids -join ','); pdfDefault = $PdfDefault;
              wallpaperColor = $WallpaperColor.ToUpperInvariant(); windowsUpdate = $WindowsUpdate }
    foreach ($k in $set.Keys) {
        if ($prof.PSObject.Properties[$k]) { $prof.$k = $set[$k] } else { $prof | Add-Member -NotePropertyName $k -NotePropertyValue $set[$k] }
    }
    New-Item -ItemType Directory -Path $customDir -Force | Out-Null
    $json = $prof | ConvertTo-Json -Depth 8
    $null = $json | ConvertFrom-Json   # 書く前に、読み直せる形か確かめる
    $tmp = $target + '.tmp'
    [System.IO.File]::WriteAllText($tmp, $json, $utf8)
    Move-Item -LiteralPath $tmp -Destination $target -Force
    Write-Result $true ('保存しました：' + $Name)
} catch {
    Write-Result $false ('保存できませんでした：' + $_.Exception.Message)
}

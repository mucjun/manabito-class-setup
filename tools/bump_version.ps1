# まなびと講習準備 ― 版の数字を 3 か所（src\main.cpp・res\app.rc・res\app.manifest）でそろえる（開発用 PC で使う）
#
# 使い方（PowerShell）:
#   cd C:\dev\ManabitoClassSetup
#   .\tools\bump_version.ps1 0.14.0

param([Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version)

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$p = $Version.Split('.')
$comma = '{0},{1},{2},0' -f $p[0], $p[1], $p[2]

function Update-File {
    param([string]$Rel, [scriptblock]$Change)
    $path = Join-Path $root $Rel
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $bom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    if ($bom) { $text = $text.Substring(1) }
    $new = & $Change $text
    if ($new -eq $text) { throw ($Rel + ' の版の場所が見つかりませんでした。') }
    [System.IO.File]::WriteAllText($path, $new, (New-Object System.Text.UTF8Encoding $bom))
    Write-Host ('そろえました: ' + $Rel)
}

Update-File 'src\main.cpp' { param($t) $t -replace 'const wchar_t kVersion\[\] = L"[0-9.]+";', ('const wchar_t kVersion[] = L"' + $Version + '";') }
Update-File 'res\app.rc' { param($t) ($t -replace '(FILEVERSION|PRODUCTVERSION) \d+,\d+,\d+,\d+', ('$1 ' + $comma)) -replace '"(FileVersion|ProductVersion)", "[0-9.]+"', ('"$1", "' + $Version + '"') }
Update-File 'res\app.manifest' { param($t) $t -replace 'name="Manabito.ClassSetup" version="[0-9.]+"', ('name="Manabito.ClassSetup" version="' + $Version + '.0"') }
Write-Host ('版を ' + $Version + ' にそろえました。docs\CHANGES_v' + $Version + '.md も書いてください。')

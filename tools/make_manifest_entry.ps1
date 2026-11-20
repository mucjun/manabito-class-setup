# まなびと講習準備 ― 目録（apps.json）に書く 1 件分を作る（開発用 PC で使う）
#
# 使い方（PowerShell）:
#   .\make_manifest_entry.ps1 -Zip ManabitoClassSetup_v0.12.0.zip -Name まなびと講習準備 -Version 0.12.0 `
#       -Url https://.../ManabitoClassSetup_v0.12.0.zip -Folder ManabitoClassSetup -Exe ManabitoClassSetup.exe
# 出てきた JSON を、apps.json の "apps" の中に貼る（前の版の行と入れ替える）。

param(
    [Parameter(Mandatory = $true)][string]$Zip,
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Url,
    [Parameter(Mandatory = $true)][string]$Folder,
    [Parameter(Mandatory = $true)][string]$Exe,
    [switch]$Shortcut
)
$hash = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash
$entry = [ordered]@{ name = $Name; version = $Version; url = $Url; sha256 = $hash; folder = $Folder; exe = $Exe }
if ($Shortcut) { $entry.shortcut = $true }
$entry | ConvertTo-Json

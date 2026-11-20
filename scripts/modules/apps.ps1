# まなびと講習準備 ― アプリの点検（入っているかどうかと版）

function Test-Office {
    param($Prof)
    $want = [string](Get-Pref $Prof 'office' 'install')
    $k = 'HKLM:\SOFTWARE\Microsoft\Office\ClickToRun\Configuration'
    $ver = Get-RegValue $k 'VersionToReport'
    $ids = Get-RegValue $k 'ProductReleaseIds'
    $plat = Get-RegValue $k 'Platform'
    $cur = '入っていない'
    if ($ver) { $cur = ('入っている（{0}、{1}）' -f $ver, $ids) }
    $detail = ('ClickToRun: VersionToReport={0} ProductReleaseIds={1} Platform={2}' -f $ver, $ids, $plat)
    if ($want -eq 'skip') {
        return New-Row 'office' 'Microsoft 365（Office）' 'INFO' $cur '入れない' $detail
    }
    $state = 'NG'; if ($ver) { $state = 'OK' }
    return New-Row 'office' 'Microsoft 365（Office）' $state $cur '入っている' $detail
}

function Test-AdobeCC {
    param($Prof)
    $want = [string](Get-Pref $Prof 'adobe' 'skip')
    $paths = @(
        (Join-Path $env:ProgramFiles 'Adobe\Adobe Creative Cloud\ACC\Creative Cloud.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Adobe\Adobe Creative Cloud\ACC\Creative Cloud.exe')
    )
    $found = $null
    foreach ($p in $paths) { if ($p -and (Test-Path -LiteralPath $p)) { $found = $p; break } }
    $cur = '入っていない'
    if ($found) {
        $v = (Get-Item -LiteralPath $found).VersionInfo.ProductVersion
        $cur = ('入っている（{0}）' -f $v)
    }
    if ($want -eq 'install') {
        $state = 'NG'; if ($found) { $state = 'OK' }
        return New-Row 'adobe' 'Adobe Creative Cloud デスクトップアプリ' $state $cur '入っている' ([string]$found)
    }
    return New-Row 'adobe' 'Adobe Creative Cloud デスクトップアプリ' 'INFO' $cur '入れない' ([string]$found)
}

# 独自アプリ：プロファイルの ownApps に書いたものを確かめる（最新版との比較は配布の目録ができてから）
function Test-OwnApps {
    param($Prof)
    $apps = @(Get-Pref $Prof 'ownApps' @())
    if ($apps.Count -eq 0) {
        return New-Row 'ownapps' '独自アプリ' 'INFO' 'プロファイルに登録なし' '' 'profiles の ownApps に、アプリの名前・フォルダー・EXE を書くと点検します。'
    }
    $rows = @()
    $i = 0
    foreach ($a in $apps) {
        $i++
        $name = [string]$a.name
        $exe = Join-Path ([string]$a.folder) ([string]$a.exe)
        if (Test-Path -LiteralPath $exe) {
            $vi = (Get-Item -LiteralPath $exe).VersionInfo
            $v = $vi.ProductVersion
            if (-not $v) { $v = $vi.FileVersion }
            if (-not $v) { $v = '版の情報なし' }
            $rows += New-Row ('ownapp' + $i) $name 'OK' ('入っている（' + $v + '）') '入っている' $exe
        } else {
            $rows += New-Row ('ownapp' + $i) $name 'NG' '入っていない' '入っている' $exe
        }
    }
    return $rows
}

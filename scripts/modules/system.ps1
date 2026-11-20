# まなびと講習準備 ― この PC の情報（画面・プリンター）

# 画面の解像度と拡大率は、画面のアプリ（EXE）が測って -Screen "幅x高さxDPI" で渡す
function Test-Screen {
    param($Prof, [string]$Screen)
    $want = Get-Pref $Prof 'screen' $null
    if ($Screen -notmatch '^(\d+)x(\d+)x(\d+)$') {
        return New-Row 'screen' '画面の解像度と拡大率' 'ERR' '' '' ('画面の情報を受け取れませんでした（"{0}"）。' -f $Screen)
    }
    $w = [int]$Matches[1]; $h = [int]$Matches[2]; $dpi = [int]$Matches[3]
    $scale = [int][Math]::Round($dpi * 100.0 / 96.0)
    $cur = ('{0}×{1}・{2}%' -f $w, $h, $scale)
    if ($null -eq $want -or [string]$want -eq 'check-only') {
        return New-Row 'screen' '画面の解像度と拡大率' 'INFO' $cur '変えない（確認のみ）' 'アプリの窓がある画面を測っています。解像度は変えない方針です。'
    }
    $ww = [int]$want.width; $wh = [int]$want.height; $ws = [int]$want.scalePercent
    $exp = ('{0}×{1}・{2}%' -f $ww, $wh, $ws)
    $state = 'NG'; if ($w -eq $ww -and $h -eq $wh -and $scale -eq $ws) { $state = 'OK' }
    return New-Row 'screen' '画面の解像度と拡大率' $state $cur $exp '教材の画面写真と見え方をそろえるための確認です。アプリの窓がある画面を測っています。'
}

function Test-DefaultPrinter {
    param($Prof)
    $p = @(Get-CimInstance -ClassName Win32_Printer -Filter 'Default=TRUE' -ErrorAction SilentlyContinue)
    if ($p.Count -eq 0) {
        return New-Row 'printer' '既定のプリンター' 'INFO' 'なし' '' ''
    }
    return New-Row 'printer' '既定のプリンター' 'INFO' ([string]$p[0].Name) '' ([string]$p[0].PortName)
}

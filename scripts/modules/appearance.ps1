# まなびと講習準備 ― 見た目と通知の点検

function ConvertTo-RgbText {
    param([string]$Hex)
    $h = $Hex.TrimStart('#')
    if ($h.Length -ne 6) { return $null }
    $r = [Convert]::ToInt32($h.Substring(0, 2), 16)
    $g = [Convert]::ToInt32($h.Substring(2, 2), 16)
    $b = [Convert]::ToInt32($h.Substring(4, 2), 16)
    return ('{0} {1} {2}' -f $r, $g, $b)
}

function ConvertTo-HexText {
    param([string]$Rgb)
    $p = @($Rgb -split '\s+' | Where-Object { $_ -ne '' })
    if ($p.Count -ne 3) { return $Rgb }
    return ('#{0:X2}{1:X2}{2:X2}' -f [int]$p[0], [int]$p[1], [int]$p[2])
}

function Test-Wallpaper {
    param($Prof)
    $want = [string](Get-Pref $Prof 'wallpaperColor' '#1F3A5F')
    $wantRgb = ConvertTo-RgbText $want
    $wp = Get-RegValue 'HKCU:\Control Panel\Desktop' 'WallPaper'
    $type = Get-RegValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Wallpapers' 'BackgroundType'
    $col = Get-RegValue 'HKCU:\Control Panel\Colors' 'Background'
    $detail = ('WallPaper="{0}" BackgroundType={1} Colors\Background="{2}"' -f $wp, $type, $col)

    if ($type -eq 2) {
        return New-Row 'wallpaper' 'デスクトップの背景' 'NG' 'スライドショー' ('無地 ' + $want) $detail
    }
    if ($wp) {
        $file = Split-Path -Leaf ([string]$wp)
        return New-Row 'wallpaper' 'デスクトップの背景' 'NG' ('画像（' + $file + '）') ('無地 ' + $want) $detail
    }
    $hex = '不明'
    if ($col) { $hex = ConvertTo-HexText ([string]$col) }
    $norm = (([string]$col) -split '\s+' | Where-Object { $_ -ne '' }) -join ' '
    $state = 'NG'; if ($norm -eq $wantRgb) { $state = 'OK' }
    return New-Row 'wallpaper' 'デスクトップの背景' $state ('無地 ' + $hex) ('無地 ' + $want) $detail
}

function Test-FileExtensions {
    param($Prof)
    $want = [bool](Get-Pref $Prof 'showFileExtensions' $true)
    $v = Get-RegValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' 'HideFileExt'
    $shown = ($v -eq 0)
    $cur = '表示しない'; if ($shown) { $cur = '表示する' }
    $exp = '表示しない'; if ($want) { $exp = '表示する' }
    $state = 'NG'; if ($shown -eq $want) { $state = 'OK' }
    return New-Row 'fileext' 'ファイルの拡張子' $state $cur $exp ('HideFileExt={0}' -f $v)
}

function Test-TaskbarWidgets {
    param($Prof)
    $want = [string](Get-Pref $Prof 'taskbarWidgets' 'hide')
    $v = Get-RegValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' 'TaskbarDa'
    # 「そろえる」はポリシー（AllowNewsAndInterests=0）で消す。TaskbarDa は Windows に書き込みを止められることがあるため。
    $policy = Get-RegValue 'HKLM:\SOFTWARE\Policies\Microsoft\Dsh' 'AllowNewsAndInterests'
    $shown = ($null -eq $v -or $v -ne 0)
    if ($policy -eq 0) { $shown = $false }
    $cur = '出さない'; if ($shown) { $cur = '出す' }
    if ($policy -eq 0) { $cur = '出さない（ポリシー）' }
    if ($want -eq 'check-only') {
        return New-Row 'widgets' 'タスクバーのウィジェット' 'NA' $cur '確認のみ' 'この版の Windows では、アプリから変えられないことがあるため対象外。消すときはタスクバーの設定から手で。'
    }
    $wantShown = ($want -eq 'show')
    $exp = '出さない'; if ($wantShown) { $exp = '出す' }
    $state = 'NG'; if ($shown -eq $wantShown) { $state = 'OK' }
    return New-Row 'widgets' 'タスクバーのウィジェット' $state $cur $exp ('TaskbarDa={0}（値が無いときは「出す」） AllowNewsAndInterests={1}' -f $v, $policy)
}

function Test-StartRecent {
    param($Prof)
    $want = [string](Get-Pref $Prof 'startRecent' 'hide')
    $v = Get-RegValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' 'Start_TrackDocs'
    $shown = ($null -eq $v -or $v -ne 0)
    $cur = '出さない'; if ($shown) { $cur = '出す' }
    if ($want -eq 'check-only') { return New-Row 'startrecent' 'スタートの「最近使用」' 'NA' $cur '確認のみ' ('Start_TrackDocs={0}' -f $v) }
    $wantShown = ($want -eq 'show')
    $exp = '出さない'; if ($wantShown) { $exp = '出す' }
    $state = 'NG'; if ($shown -eq $wantShown) { $state = 'OK' }
    return New-Row 'startrecent' 'スタートの「最近使用」' $state $cur $exp ('Start_TrackDocs={0}（値が無いときは「出す」）' -f $v)
}

function Test-TaskbarSearch {
    param($Prof)
    $names = @{ 0 = '出さない'; 1 = 'アイコンだけ'; 2 = '検索ボックス'; 3 = 'アイコンと文字' }
    $wantKey = [string](Get-Pref $Prof 'taskbarSearch' 'icon')
    $wantMap = @{ 'hide' = 0; 'icon' = 1; 'box' = 2; 'label' = 3 }
    $v = Get-RegValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Search' 'SearchboxTaskbarMode'
    $cur = 'Windows の既定'
    if ($null -ne $v -and $names.ContainsKey([int]$v)) { $cur = $names[[int]$v] }
    if ($wantKey -eq 'check-only' -or -not $wantMap.ContainsKey($wantKey)) {
        return New-Row 'search' 'タスクバーの検索' 'INFO' $cur '確認のみ' ('SearchboxTaskbarMode={0}' -f $v)
    }
    $want = $wantMap[$wantKey]
    $state = 'NG'; if ($null -ne $v -and [int]$v -eq $want) { $state = 'OK' }
    return New-Row 'search' 'タスクバーの検索' $state $cur $names[$want] ('SearchboxTaskbarMode={0}' -f $v)
}

function Test-Notifications {
    param($Prof)
    $v = Get-RegValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\PushNotifications' 'ToastEnabled'
    $on = ($null -eq $v -or $v -ne 0)
    $cur = '出さない'; if ($on) { $cur = '出す' }
    $state = 'NG'; if (-not $on) { $state = 'OK' }
    return New-Row 'toast' '通知（右下のお知らせ）' $state $cur '出さない' ('ToastEnabled={0}（値が無いときは「出す」）' -f $v)
}

function Test-OneDriveStartup {
    param($Prof)
    $run = Get-RegValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' 'OneDrive'
    if (-not $run) {
        $saved = Get-RegValue 'HKCU:\Software\ManabitoClassSetup' 'OneDriveRun'
        $note = 'Run に OneDrive がありません。'
        if ($saved) { $note += '（このアプリが外しました。元の値は HKCU\Software\ManabitoClassSetup の OneDriveRun に控えています）' }
        return New-Row 'onedrive' 'OneDrive の自動起動' 'OK' '起動しない' '起動しない' $note
    }
    # スタートアップアプリの設定で無効にされていれば、先頭の 1 バイトが 2 以外（3 など）
    $approved = Get-RegValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run' 'OneDrive'
    $disabled = $false
    if ($approved -is [byte[]] -and $approved.Length -gt 0 -and $approved[0] -ne 2) { $disabled = $true }
    $bytesText = '（なし）'
    if ($approved -is [byte[]]) { $bytesText = (($approved | ForEach-Object { '{0:X2}' -f $_ }) -join ' ') }
    $detail = ('Run="{0}" StartupApproved="{1}"' -f $run, $bytesText)
    if ($disabled) {
        return New-Row 'onedrive' 'OneDrive の自動起動' 'OK' '起動しない（無効にしてある）' '起動しない' $detail
    }
    return New-Row 'onedrive' 'OneDrive の自動起動' 'NG' '起動する' '起動しない' $detail
}

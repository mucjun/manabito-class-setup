# まなびと講習準備 ― 設定をそろえる（元に戻せる設定だけ。履歴やファイルは消さない）
#
# 各関数は 1 行の結果を返す。State は
#   DONE=変えた  SAME=もともとそろっていた  SKIP=対象外・見送り  FAIL=失敗

$script:NeedExplorerRestart = $false

function Set-RegDword {
    param([string]$Path, [string]$Name, [int]$Value)
    if (-not (Test-Path -LiteralPath $Path)) { New-Item -Path $Path -Force -ErrorAction Stop | Out-Null }
    New-ItemProperty -LiteralPath $Path -Name $Name -Value $Value -PropertyType DWord -Force -ErrorAction Stop | Out-Null
}

function Remove-RegValue {
    param([string]$Path, [string]$Name)
    if ((Test-Path -LiteralPath $Path) -and $null -ne (Get-RegValue $Path $Name)) {
        Remove-ItemProperty -LiteralPath $Path -Name $Name -Force -ErrorAction Stop
        return $true
    }
    return $false
}

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# ---------------------------------------------------------------- 電源

function Set-PowerTimeouts {
    param($Prof)
    $items = @(
        @('STANDBYIDLE',   'standby-timeout',   '自動スリープ'),
        @('VIDEOIDLE',     'monitor-timeout',   '画面の自動消灯'),
        @('HIBERNATEIDLE', 'hibernate-timeout', '自動の休止状態')
    )
    $sub = @{ 'STANDBYIDLE' = 'SUB_SLEEP'; 'VIDEOIDLE' = 'SUB_VIDEO'; 'HIBERNATEIDLE' = 'SUB_SLEEP' }
    $changed = @(); $failed = @(); $notes = @()
    foreach ($it in $items) {
        $v = Get-PowerIndex $sub[$it[0]] $it[0]
        if ($null -eq $v) { $notes += ($it[2] + '：設定なし'); continue }
        if ($v.AC -eq 0 -and $v.DC -eq 0) { continue }
        & powercfg.exe /change ($it[1] + '-ac') 0 2>$null | Out-Null
        $okAc = ($LASTEXITCODE -eq 0)
        & powercfg.exe /change ($it[1] + '-dc') 0 2>$null | Out-Null
        $okDc = ($LASTEXITCODE -eq 0)
        if ($okAc -and $okDc) { $changed += $it[2] } else { $failed += $it[2] }
    }
    $detail = ($notes -join '、')
    if ($failed.Count -gt 0) {
        return New-Row 'power' '電源（スリープ・消灯・休止）' 'FAIL' '' 'しない' ('powercfg で変えられなかったもの：' + ($failed -join '、'))
    }
    if ($changed.Count -gt 0) {
        return New-Row 'power' '電源（スリープ・消灯・休止）' 'DONE' ($changed -join '、') 'しない' $detail
    }
    return New-Row 'power' '電源（スリープ・消灯・休止）' 'SAME' '' 'しない' $detail
}

# ふたを閉じたとき（電源接続時）は「何もしない」。電池の無い PC は対象外。
function Set-LidAction {
    param($Prof)
    $before = Test-LidAction $Prof
    if ($before.State -eq 'NA') { return New-Row 'lid' 'ふたを閉じたとき' 'SKIP' $before.Current $before.Expected '' }
    if ($before.State -eq 'OK') { return New-Row 'lid' 'ふたを閉じたとき' 'SAME' $before.Current $before.Expected '' }
    & powercfg.exe /setacvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0 2>$null | Out-Null
    $ok = ($LASTEXITCODE -eq 0)
    & powercfg.exe /setactive SCHEME_CURRENT 2>$null | Out-Null
    if (-not $ok) { return New-Row 'lid' 'ふたを閉じたとき' 'FAIL' $before.Current '電源接続時は何もしない' 'powercfg で変えられませんでした。' }
    return New-Row 'lid' 'ふたを閉じたとき' 'DONE' $before.Current '電源接続時は何もしない' '電池で動いているときの動作は変えていません。'
}

# ---------------------------------------------------------------- 見た目と通知

$script:DesktopCode = @'
using System;
using System.Runtime.InteropServices;
namespace ManabitoDesk {
    public static class Native {
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern bool SystemParametersInfo(uint action, uint uParam, string vParam, uint winIni);
        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool SetSysColors(int count, int[] elements, int[] colors);
    }
}
'@

function Set-Wallpaper {
    param($Prof)
    $want = [string](Get-Pref $Prof 'wallpaperColor' '#1F3A5F')
    $rgb = ConvertTo-RgbText $want
    if (-not $rgb) { return New-Row 'wallpaper' 'デスクトップの背景' 'FAIL' '' $want ('色の書き方が正しくありません：' + $want) }
    $before = Test-Wallpaper $Prof
    if ($before.State -eq 'OK') { return New-Row 'wallpaper' 'デスクトップの背景' 'SAME' $before.Current $before.Expected '' }

    New-ItemProperty -LiteralPath 'HKCU:\Control Panel\Colors' -Name 'Background' -Value $rgb -PropertyType String -Force -ErrorAction Stop | Out-Null
    Set-ItemProperty -LiteralPath 'HKCU:\Control Panel\Desktop' -Name 'WallPaper' -Value '' -ErrorAction Stop
    Set-RegDword 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Wallpapers' 'BackgroundType' 1

    if (-not ('ManabitoDesk.Native' -as [type])) { Add-Type -TypeDefinition $script:DesktopCode -Language CSharp -ErrorAction Stop }
    # 画像を外す（SPI_SETDESKWALLPAPER、保存して全体に知らせる）
    [ManabitoDesk.Native]::SystemParametersInfo(0x0014, 0, '', 0x01 -bor 0x02) | Out-Null
    # 色を今すぐ反映（COLOR_BACKGROUND = 1、COLORREF は 0x00BBGGRR）
    $p = @($rgb -split ' ' | ForEach-Object { [int]$_ })
    $colorref = $p[0] -bor ($p[1] -shl 8) -bor ($p[2] -shl 16)
    [ManabitoDesk.Native]::SetSysColors(1, [int[]]@(1), [int[]]@($colorref)) | Out-Null
    return New-Row 'wallpaper' 'デスクトップの背景' 'DONE' $before.Current ('無地 ' + $want) ''
}

function Set-FileExtensions {
    param($Prof)
    $want = [bool](Get-Pref $Prof 'showFileExtensions' $true)
    $before = Test-FileExtensions $Prof
    if ($before.State -eq 'OK') { return New-Row 'fileext' 'ファイルの拡張子' 'SAME' $before.Current $before.Expected '' }
    $v = 1; if ($want) { $v = 0 }
    Set-RegDword 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' 'HideFileExt' $v
    $script:NeedExplorerRestart = $true
    return New-Row 'fileext' 'ファイルの拡張子' 'DONE' $before.Current $before.Expected ''
}

function Set-TaskbarSearch {
    param($Prof)
    $wantKey = [string](Get-Pref $Prof 'taskbarSearch' 'icon')
    $wantMap = @{ 'hide' = 0; 'icon' = 1; 'box' = 2; 'label' = 3 }
    if (-not $wantMap.ContainsKey($wantKey)) { return New-Row 'search' 'タスクバーの検索' 'SKIP' '' '確認のみ' '' }
    $before = Test-TaskbarSearch $Prof
    if ($before.State -eq 'OK') { return New-Row 'search' 'タスクバーの検索' 'SAME' $before.Current $before.Expected '' }
    Set-RegDword 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Search' 'SearchboxTaskbarMode' $wantMap[$wantKey]
    $script:NeedExplorerRestart = $true
    return New-Row 'search' 'タスクバーの検索' 'DONE' $before.Current $before.Expected ''
}

# ウィジェットは TaskbarDa ではなくポリシーで消す（TaskbarDa は Windows に書き込みを止められることがある）
function Set-TaskbarWidgets {
    param($Prof)
    $want = [string](Get-Pref $Prof 'taskbarWidgets' 'hide')
    if ($want -eq 'check-only') { return New-Row 'widgets' 'タスクバーのウィジェット' 'SKIP' '' '確認のみ' '' }
    $before = Test-TaskbarWidgets $Prof
    if ($before.State -eq 'OK') { return New-Row 'widgets' 'タスクバーのウィジェット' 'SAME' $before.Current $before.Expected '' }
    $key = 'HKLM:\SOFTWARE\Policies\Microsoft\Dsh'
    try {
        if ($want -eq 'show') {
            Remove-RegValue $key 'AllowNewsAndInterests' | Out-Null
        } else {
            Set-RegDword $key 'AllowNewsAndInterests' 0
        }
    } catch {
        # 新しい Windows（26H2 で確認）では、この書き込みが止められる。失敗にはせず、手で消す方法を案内する
        return New-Row 'widgets' 'タスクバーのウィジェット' 'SKIP' $before.Current $before.Expected 'この版の Windows では、アプリからは変えられません。手で消すときは、タスクバーを右クリック →「タスクバーの設定」→「ウィジェット」をオフ。'
    }
    $script:NeedExplorerRestart = $true
    return New-Row 'widgets' 'タスクバーのウィジェット' 'DONE' $before.Current $before.Expected ''
}

# スタートの「おすすめ（最近使用）」に、最近使ったファイルやおすすめを出さない（受講中に開いたファイルが次の受講生に見えないように）
function Set-StartRecent {
    param($Prof)
    $want = [string](Get-Pref $Prof 'startRecent' 'hide')
    if ($want -eq 'check-only') { return New-Row 'startrecent' 'スタートの「最近使用」' 'SKIP' '' '確認のみ' '' }
    $before = Test-StartRecent $Prof
    if ($before.State -eq 'OK') { return New-Row 'startrecent' 'スタートの「最近使用」' 'SAME' $before.Current $before.Expected '' }
    $v = 0; if ($want -eq 'show') { $v = 1 }
    try {
        Set-RegDword 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' 'Start_TrackDocs' $v
        Set-RegDword 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' 'Start_IrisRecommendations' $v
    } catch {
        return New-Row 'startrecent' 'スタートの「最近使用」' 'SKIP' $before.Current $before.Expected ('アプリからは変えられませんでした。手で変えるときは、設定 →「個人用設定」→「スタート」で「最近開いた項目を…表示する」をオフ。（' + $_.Exception.Message + '）')
    }
    $script:NeedExplorerRestart = $true
    return New-Row 'startrecent' 'スタートの「最近使用」' 'DONE' $before.Current $before.Expected 'エクスプローラーのジャンプリストとクイック アクセスの「最近使用した項目」にも出なくなります。'
}

function Set-DeliveryOptimization {
    param($Prof)
    $want = [string](Get-Pref $Prof 'deliveryOptimization' 'lan')
    $name = '更新の LAN での分け合い（配信の最適化）'
    if ($want -eq 'check-only') { return New-Row 'dolan' $name 'SKIP' '' '確認のみ' '' }
    $before = Test-DeliveryOptimization $Prof
    if ($before.State -eq 'OK') { return New-Row 'dolan' $name 'SAME' $before.Current $before.Expected '' }
    try {
        Set-RegDword 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\DeliveryOptimization' 'DODownloadMode' 1
    } catch {
        return New-Row 'dolan' $name 'SKIP' $before.Current $before.Expected ('この PC では変えられませんでした：' + $_.Exception.Message)
    }
    return New-Row 'dolan' $name 'DONE' $before.Current $before.Expected 'グループ ポリシー（ダウンロード モード＝LAN）と同じ値を書きました。'
}

function Set-Notifications {
    param($Prof)
    $before = Test-Notifications $Prof
    if ($before.State -eq 'OK') { return New-Row 'toast' '通知（右下のお知らせ）' 'SAME' $before.Current $before.Expected '' }
    Set-RegDword 'HKCU:\Software\Microsoft\Windows\CurrentVersion\PushNotifications' 'ToastEnabled' 0
    return New-Row 'toast' '通知（右下のお知らせ）' 'DONE' $before.Current $before.Expected ''
}

# OneDrive の自動起動は、Run の登録を外して止める（OneDrive 本体は消さない）。
# v0.4.0 まではスタートアップアプリの「無効」の印（StartupApproved）を書いていたが、
# 実機で書いたそばから消されたため、登録そのものを外す方法に変えた。
# 元の値は HKCU\Software\ManabitoClassSetup の OneDriveRun に控え、念のため「無効」の印も書いておく。
function Set-OneDriveStartup {
    param($Prof)
    $before = Test-OneDriveStartup $Prof
    if ($before.State -eq 'OK') { return New-Row 'onedrive' 'OneDrive の自動起動' 'SAME' $before.Current $before.Expected '' }
    $runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    $run = Get-RegValue $runKey 'OneDrive'
    # OneDrive が動いたままだと、自分で登録し直すことがある（実機で確認）ので、先に閉じる
    $sid = (Get-Process -Id $PID).SessionId
    $od = @(Get-Process -Name 'OneDrive' -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sid })
    foreach ($o in $od) { Stop-Process -Id $o.Id -Force -ErrorAction SilentlyContinue }
    if ($od.Count -gt 0) { Start-Sleep -Seconds 2 }
    $keep = 'HKCU:\Software\ManabitoClassSetup'
    if (-not (Test-Path -LiteralPath $keep)) { New-Item -Path $keep -Force -ErrorAction Stop | Out-Null }
    New-ItemProperty -LiteralPath $keep -Name 'OneDriveRun' -Value ([string]$run) -PropertyType String -Force -ErrorAction Stop | Out-Null
    Remove-ItemProperty -LiteralPath $runKey -Name 'OneDrive' -Force -ErrorAction Stop
    try {
        $sa = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run'
        if (-not (Test-Path -LiteralPath $sa)) { New-Item -Path $sa -Force | Out-Null }
        New-ItemProperty -LiteralPath $sa -Name 'OneDrive' -Value ([byte[]](3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) -PropertyType Binary -Force | Out-Null
    } catch { }
    Start-Sleep -Milliseconds 500
    $after = Test-OneDriveStartup $Prof
    if ($after.State -ne 'OK') {
        return New-Row 'onedrive' 'OneDrive の自動起動' 'FAIL' $before.Current $before.Expected ('外したあと読み直すと、また登録されていました。' + $after.Detail)
    }
    return New-Row 'onedrive' 'OneDrive の自動起動' 'DONE' $before.Current $before.Expected 'OneDrive を閉じてから外しました。次回のサインインから起動しません（OneDrive を手で起動すると、登録し直されることがあります）。'
}

# ---------------------------------------------------------------- 音

function Set-VolumeMute {
    param($Prof)
    $before = Test-Volume $Prof
    if ($before.State -eq 'OK') { return New-Row 'volume' '音量' 'SAME' $before.Current $before.Expected '' }
    if ($before.State -eq 'NA') { return New-Row 'volume' '音量' 'SKIP' $before.Current $before.Expected $before.Detail }
    if ($before.State -eq 'ERR') { return New-Row 'volume' '音量' 'FAIL' '' '消音' $before.Detail }
    [ManabitoAudio.Volume]::SetMute($true)
    return New-Row 'volume' '音量' 'DONE' $before.Current '消音' ''
}

# ---------------------------------------------------------------- Windows Update

function Set-WindowsUpdate {
    param($Prof)
    $want = [string](Get-Pref $Prof 'windowsUpdate' 'check-only')
    if ($want -ne 'stop' -and $want -ne 'run') { return New-Row 'wu' 'Windows Update' 'SKIP' '' '確認のみ' '' }
    $before = Test-WindowsUpdate $Prof
    if ($before.State -eq 'OK') { return New-Row 'wu' 'Windows Update' 'SAME' $before.Current $before.Expected '' }

    $au = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU'
    $ux = 'HKLM:\SOFTWARE\Microsoft\WindowsUpdate\UX\Settings'
    $pauseNames = @('PauseUpdatesStartTime', 'PauseUpdatesExpiryTime',
                    'PauseFeatureUpdatesStartTime', 'PauseFeatureUpdatesEndTime',
                    'PauseQualityUpdatesStartTime', 'PauseQualityUpdatesEndTime')
    $e = Get-WindowsEditionInfo

    if ($want -eq 'run') {
        Remove-RegValue $au 'NoAutoUpdate' | Out-Null
        Remove-RegValue $au 'NoAutoRebootWithLoggedOnUsers' | Out-Null
        foreach ($n in $pauseNames) { Remove-RegValue $ux $n | Out-Null }
        return New-Row 'wu' 'Windows Update' 'DONE' $before.Current '動かす' 'ポリシーと一時停止を外しました。'
    }

    if ($e.IsHome) {
        # Home はポリシーが効かないため、一時停止（35 日）で代用する
        $now = (Get-Date).ToUniversalTime()
        $start = $now.ToString('yyyy-MM-ddTHH:mm:ssZ')
        $end = $now.AddDays(35).ToString('yyyy-MM-ddTHH:mm:ssZ')
        if (-not (Test-Path -LiteralPath $ux)) { New-Item -Path $ux -Force -ErrorAction Stop | Out-Null }
        foreach ($n in @('PauseUpdatesStartTime', 'PauseFeatureUpdatesStartTime', 'PauseQualityUpdatesStartTime')) {
            New-ItemProperty -LiteralPath $ux -Name $n -Value $start -PropertyType String -Force -ErrorAction Stop | Out-Null
        }
        foreach ($n in @('PauseUpdatesExpiryTime', 'PauseFeatureUpdatesEndTime', 'PauseQualityUpdatesEndTime')) {
            New-ItemProperty -LiteralPath $ux -Name $n -Value $end -PropertyType String -Force -ErrorAction Stop | Out-Null
        }
        return New-Row 'wu' 'Windows Update' 'DONE' $before.Current '一時停止（35 日）' 'Home のため、止める代わりに一時停止にしました。35 日たつと自動で動き出します。'
    }

    Set-RegDword $au 'NoAutoUpdate' 1
    Set-RegDword $au 'NoAutoRebootWithLoggedOnUsers' 1
    return New-Row 'wu' 'Windows Update' 'DONE' $before.Current '止める' 'グループ ポリシー（自動更新を構成する＝無効）と同じ値を書きました。設定アプリには「組織が管理」と出ます。'
}

# ---------------------------------------------------------------- エクスプローラーの再起動

function Restart-UserExplorer {
    $sid = (Get-Process -Id $PID).SessionId
    $ex = @(Get-Process -Name explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sid })
    foreach ($p in $ex) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    # 通常は Windows が自動で起動し直す。5 秒待っても出てこなければ起動する。
    for ($i = 0; $i -lt 10; $i++) {
        Start-Sleep -Milliseconds 500
        if (@(Get-Process -Name explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sid }).Count -gt 0) { return $true }
    }
    Start-Process -FilePath (Join-Path $env:WINDIR 'explorer.exe')
    return $false
}

# ---------------------------------------------------------------- まとめて（apply.ps1・oneclick.ps1 から使う）

# 元に戻せる設定をすべてそろえる。結果の行を $Rows に足す（戻り値は無し）。
function Invoke-ApplyAll {
    param($Prof, [System.Collections.Generic.List[object]]$Rows)
    $steps = @(
        @('Set-PowerTimeouts',   '電源（スリープ・消灯・休止）'),
        @('Set-LidAction',       'ふたを閉じたとき'),
        @('Set-Wallpaper',       'デスクトップの背景'),
        @('Set-FileExtensions',  'ファイルの拡張子'),
        @('Set-TaskbarWidgets',  'タスクバーのウィジェット'),
        @('Set-TaskbarSearch',   'タスクバーの検索'),
        @('Set-StartRecent',     'スタートの「最近使用」'),
        @('Set-WindowsAds',      'Windows の広告・おすすめ・ヒント'),
        @('Set-Copilot',         'Copilot'),
        @('Set-EdgeFirstRun',    'Edge の最初の設定の画面'),
        @('Set-LockScreenPhoto', 'ロック画面・サインイン画面の写真'),
        @('Set-QuickAccessShow', 'エクスプローラーの履歴の表示'),
        @('Set-Notifications',   '通知（右下のお知らせ）'),
        @('Set-OneDriveStartup', 'OneDrive の自動起動'),
        @('Set-VolumeMute',      '音量'),
        @('Set-WindowsUpdate',   'Windows Update'),
        @('Set-DeliveryOptimization', '更新の LAN での分け合い'),
        @('Set-Printers',        'プリンター（先生用から受け取る）'),
        @('Set-ExtraAppSettings', 'この講座のアプリの設定')
    )
    $script:NeedExplorerRestart = $false
    foreach ($s in $steps) {
        $func = $s[0]; $label = $s[1]
        try {
            $rs = @(& $func $Prof)   # 1 つの項目から複数の行が返ることがある（この講座のアプリの設定など）
        } catch {
            $rs = @(New-Row ($func.ToLower()) $label 'FAIL' '' '' ('変更の途中で問題が起きました：' + $_.Exception.Message))
        }
        foreach ($r in $rs) {
            if ($null -eq $r -or -not $r.PSObject.Properties['State']) { continue }
            [void]$Rows.Add($r)
            Write-Host ('[{0}] {1} ／ {2} → {3} ／ {4}' -f $r.State, $r.Name, $r.Current, $r.Expected, $r.Detail)
        }
    }
    if ($script:NeedExplorerRestart) {
        $ok = Restart-UserExplorer
        $note = 'タスクバーと拡張子の設定を反映するため、エクスプローラーを起動し直しました。'
        if (-not $ok) { $note += '（自動で起動し直さなかったため、こちらで起動しました）' }
        [void]$Rows.Add((New-Row 'explorer' 'エクスプローラーの再起動' 'DONE' '' '' $note))
        Write-Host $note
    }
}

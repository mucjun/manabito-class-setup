# まなびと講習準備 ― 先生の PC の講習後の片付け（v0.20.0）
#
# 複数の講師が同じローカル ユーザーで使う先生の PC を、次の講習に備えて整える。
#   1. デスクトップ・ダウンロード・ドキュメント・スクリーンショット・画面の録画を「ドキュメント\講習の保管\日時」に移す（消さない）
#   2. 受講生の PC と同じ［履歴を消す］（サインアウト・履歴・IME・ブラウザー）。ただしダウンロード・デスクトップ・授業ファイルは消さない（1 で移す）
#   3. 画面を「複製」に、音の出口を PC のスピーカーに、既定のプリンターを先生用で選んだものに戻す
#   4. クリップボードの履歴を消す。Wi-Fi の接続先のうち、教室の基準に無いものを消す（初回は基準を覚えるだけ）
# Bluetooth は画面のアプリ（C++）が行う（管理者が要らないため）。

function Get-ArchiveRoot {
    return (Join-Path ([Environment]::GetFolderPath('MyDocuments')) '講習の保管')
}

function Get-KnownDownloads {
    try {
        $v = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders' -ErrorAction Stop).'{374DE290-123F-4565-9164-39C4925E467B}'
        if ($v) { return [Environment]::ExpandEnvironmentVariables([string]$v) }
    } catch { }
    if ($env:USERPROFILE) { return (Join-Path $env:USERPROFILE 'Downloads') }
    return ''
}

# 1 つの場所の中身を、保管のフォルダーへ移す。戻り値は結果の行
function Move-AreaToArchive {
    param([string]$Id, [string]$Name, [string]$Source, [string]$Dest, [switch]$KeepShortcuts, [string]$ExcludePath = '')
    if (-not $Source -or -not (Test-Path -LiteralPath $Source -PathType Container)) { return New-Row $Id $Name 'SKIP' 'なし' '空' ('場所がありません：' + $Source) }
    $items = @(Get-ChildItem -LiteralPath $Source -Force -ErrorAction SilentlyContinue | Where-Object {
        $a = $_.Attributes
        $skip = ($_.Name -ieq 'desktop.ini') -or
                (($a -band [IO.FileAttributes]::ReparsePoint) -ne 0) -or                                         # OneDrive のクラウドだけのもの・ジャンクション
                ((($a -band [IO.FileAttributes]::Hidden) -ne 0) -and (($a -band [IO.FileAttributes]::System) -ne 0)) -or
                ($KeepShortcuts -and ($_.Extension -in @('.lnk', '.url'))) -or
                ($ExcludePath -and ($_.FullName.TrimEnd('\') -ieq $ExcludePath.TrimEnd('\'))) -or
                (Test-IsProtected $_.FullName)
        -not $skip
    })
    if ($items.Count -eq 0) { return New-Row $Id $Name 'SAME' '0 個' '空' '' }
    $target = Join-Path $Dest $Name
    try { New-Item -ItemType Directory -Path $target -Force -ErrorAction Stop | Out-Null } catch { return New-Row $Id $Name 'FAIL' ('{0} 個' -f $items.Count) '空' ('保管のフォルダーを作れませんでした：' + $_.Exception.Message) }
    $sameDrive = ([IO.Path]::GetPathRoot($Source) -ieq [IO.Path]::GetPathRoot($target))
    $moved = 0; $failed = @()
    foreach ($it in $items) {
        try {
            if ($it.PSIsContainer -and -not $sameDrive) { throw '別のドライブへのフォルダーの移動はしません（途中で止まると半分だけ移るため）。' }
            Move-Item -LiteralPath $it.FullName -Destination (Join-Path $target $it.Name) -Force -ErrorAction Stop
            $moved++
        } catch {
            $failed += ($it.Name + '（' + $_.Exception.Message + '）')
        }
    }
    if ($failed.Count -gt 0) {
        return New-Row $Id $Name 'FAIL' ('{0} 個' -f $items.Count) '空' ('{0} 個を移しました。移せなかったもの（消さずに残しています）：{1}' -f $moved, ($failed -join '、'))
    }
    return New-Row $Id $Name 'DONE' ('{0} 個' -f $items.Count) '空' ('移した先：' + $target)
}

function Move-TeacherFiles {
    param($Prof)
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'   # 秒まで（続けて片付けても同じフォルダーにならない）
    $root = Get-ArchiveRoot
    $dest = Join-Path $root $stamp
    $rows = @()
    $docs = [Environment]::GetFolderPath('MyDocuments')
    $rows += Move-AreaToArchive 'archive-desktop' 'デスクトップ' ([Environment]::GetFolderPath('Desktop')) $dest -KeepShortcuts
    $rows += Move-AreaToArchive 'archive-downloads' 'ダウンロード' (Get-KnownDownloads) $dest
    $rows += Move-AreaToArchive 'archive-documents' 'ドキュメント' $docs $dest -ExcludePath $root
    $pics = [Environment]::GetFolderPath('MyPictures')
    if ($pics) { $rows += Move-AreaToArchive 'archive-screenshots' 'スクリーンショット' (Join-Path $pics 'Screenshots') $dest }
    $vids = [Environment]::GetFolderPath('MyVideos')
    if ($vids) { $rows += Move-AreaToArchive 'archive-captures' '画面の録画' (Join-Path $vids 'Captures') $dest }
    $sites = Get-LocalSitesPath
    if ($sites -and (Test-Path -LiteralPath $sites)) {
        $null = Stop-LocalWpProcesses   # サイトを動かす部品がファイルをつかんでいると移せないため
        $rows += Move-AreaToArchive 'archive-localsites' 'Local のサイト' $sites $dest
    }
    return $rows
}

# ---- 画面・音・プリンター
function Set-DisplayClone {
    param($Prof)
    $name = '画面の出し方（複製）'
    if (-not $env:WINDIR) { return New-Row 'display' $name 'SKIP' '' '複製' '' }
    $exe = Join-Path $env:WINDIR 'System32\DisplaySwitch.exe'
    if (-not (Test-Path -LiteralPath $exe)) { return New-Row 'display' $name 'SKIP' '' '複製' 'DisplaySwitch.exe がありません。' }
    try { Start-Process -FilePath $exe -ArgumentList '/clone' -Wait -ErrorAction Stop } catch { return New-Row 'display' $name 'FAIL' '' '複製' $_.Exception.Message }
    return New-Row 'display' $name 'DONE' '' '複製' 'プロジェクターなどがつながっていれば、同じ画面を出す設定にしました（つながっていなければ何も変わりません）。'
}

$script:PolicyConfigSource = @'
using System;
using System.Runtime.InteropServices;
namespace ManabitoAudio {
    [ComImport, Guid("f8679f50-850a-41cf-9c72-430f290290c8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IPolicyConfig {
        [PreserveSig] int GetMixFormat([MarshalAs(UnmanagedType.LPWStr)] string a, IntPtr b);
        [PreserveSig] int GetDeviceFormat([MarshalAs(UnmanagedType.LPWStr)] string a, int b, IntPtr c);
        [PreserveSig] int ResetDeviceFormat([MarshalAs(UnmanagedType.LPWStr)] string a);
        [PreserveSig] int SetDeviceFormat([MarshalAs(UnmanagedType.LPWStr)] string a, IntPtr b, IntPtr c);
        [PreserveSig] int GetProcessingPeriod([MarshalAs(UnmanagedType.LPWStr)] string a, int b, IntPtr c, IntPtr d);
        [PreserveSig] int SetProcessingPeriod([MarshalAs(UnmanagedType.LPWStr)] string a, IntPtr b);
        [PreserveSig] int GetShareMode([MarshalAs(UnmanagedType.LPWStr)] string a, IntPtr b);
        [PreserveSig] int SetShareMode([MarshalAs(UnmanagedType.LPWStr)] string a, IntPtr b);
        [PreserveSig] int GetPropertyValue([MarshalAs(UnmanagedType.LPWStr)] string a, IntPtr b, IntPtr c);
        [PreserveSig] int SetPropertyValue([MarshalAs(UnmanagedType.LPWStr)] string a, IntPtr b, IntPtr c);
        [PreserveSig] int SetDefaultEndpoint([MarshalAs(UnmanagedType.LPWStr)] string id, int role);
        [PreserveSig] int SetEndpointVisibility([MarshalAs(UnmanagedType.LPWStr)] string a, int b);
    }
    [ComImport, Guid("870af99c-171d-4f9e-af0d-e63df40c2bc9")]
    class PolicyConfigClient { }
    public static class Default {
        public static int Set(string id) {
            var pc = (IPolicyConfig)(new PolicyConfigClient());
            int hr = 0;
            for (int role = 0; role < 3; role++) { int r = pc.SetDefaultEndpoint(id, role); if (r != 0) hr = r; }   // 本体・通信・マルチメディアの 3 つ
            return hr;
        }
    }
}
'@

# 再生の出口のうち、PC の「スピーカー」（形の印が Speakers）で、使えるものを探す。HDMI・DisplayPort（ディスプレイの音）は選ばない
function Find-SpeakerEndpoint {
    $base = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render'
    if (-not (Test-Path -LiteralPath $base)) { return $null }
    foreach ($k in @(Get-ChildItem -LiteralPath $base -ErrorAction SilentlyContinue)) {
        $state = $k.GetValue('DeviceState', 0)
        if ([int]$state -ne 1) { continue }   # 1 = 使える
        $props = Join-Path $k.PSPath 'Properties'
        try { $p = Get-ItemProperty -LiteralPath $props -ErrorAction Stop } catch { continue }
        $form = $p.'{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},0'
        if ($null -eq $form -or [int]$form -ne 1) { continue }   # 1 = Speakers（9 は HDMI などのディスプレイ）
        $nameA = [string]$p.'{a45c254e-df1c-4efd-8020-67d146a850e0},2'
        $nameB = [string]$p.'{b3f8fa53-0004-438e-9003-51a46e139bfc},6'
        return @{ Id = ('{0.0.0.00000000}.' + $k.PSChildName); Name = (($nameA + '（' + $nameB + '）').Trim('（）')) }
    }
    return $null
}

function Set-DefaultSpeakers {
    param($Prof)
    $name = '音の出口（PC のスピーカー）'
    $sp = Find-SpeakerEndpoint
    if (-not $sp) { return New-Row 'speakers' $name 'SKIP' '' 'PC のスピーカー' 'PC のスピーカーが見つかりませんでした。' }
    try {
        if (-not ('ManabitoAudio.Default' -as [type])) { Add-Type -TypeDefinition $script:PolicyConfigSource -ErrorAction Stop }
        $hr = [ManabitoAudio.Default]::Set($sp.Id)
        if ($hr -ne 0) { throw ('エラー ' + $hr) }
    } catch {
        return New-Row 'speakers' $name 'SKIP' '' $sp.Name ('切り替えられませんでした（' + $_.Exception.Message + '）。タスクバーの音のアイコンから選んでください。')
    }
    return New-Row 'speakers' $name 'DONE' '' $sp.Name ('音の出口を「' + $sp.Name + '」にしました。')
}

function Set-TeacherDefaultPrinter {
    param($Prof)
    $name = '既定のプリンター'
    $file = $null
    if ($env:LOCALAPPDATA) { $file = Join-Path $env:LOCALAPPDATA 'ManabitoClassSetup\printers_shared.txt' }
    $want = ''
    if ($file -and (Test-Path -LiteralPath $file)) {
        foreach ($l in @(Get-Content -LiteralPath $file -Encoding UTF8)) {
            $f = ([string]$l).TrimStart([char]0xFEFF).Split('|')
            if ($f.Count -ge 7 -and $f[0] -eq 'p' -and $f[6] -eq '1') { $want = $f[1] }
        }
    }
    if (-not $want) { return New-Row 'teacherprinter' $name 'SKIP' '' '先生用で選んだもの' '先生用の［プリンター］タブで「既定にする」を選んで配ると、そのプリンターを既定に戻します。' }
    if (-not (Get-Printer -Name $want -ErrorAction SilentlyContinue)) { return New-Row 'teacherprinter' $name 'SKIP' '' $want ('「' + $want + '」はこの PC に入っていません。') }
    try {
        Set-RegDword 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Windows' 'LegacyDefaultPrinterMode' 1
        $w = Get-CimInstance -ClassName Win32_Printer -Filter ("Name='{0}'" -f ($want -replace "'", "''")) -ErrorAction Stop
        $r = Invoke-CimMethod -InputObject $w -MethodName SetDefaultPrinter -ErrorAction Stop
        if ($r.ReturnValue -ne 0) { throw ('エラー ' + $r.ReturnValue) }
    } catch { return New-Row 'teacherprinter' $name 'FAIL' '' $want $_.Exception.Message }
    return New-Row 'teacherprinter' $name 'DONE' '' $want ''
}

# ---- クリップボード
function Clear-ClipboardHistory {
    param($Prof)
    $name = 'クリップボードの履歴（Win＋V）'
    $note = ''
    try { & cmd.exe /c 'echo off | clip' } catch { }   # 今のクリップボードを空に
    try {
        $null = [Windows.ApplicationModel.DataTransfer.Clipboard, Windows.ApplicationModel.DataTransfer, ContentType = WindowsRuntime]
        $ok = [Windows.ApplicationModel.DataTransfer.Clipboard]::ClearHistory()
        if (-not $ok) { $note = '履歴は消せませんでした。Win＋V を押し、「すべてクリア」を押してください。' }
    } catch { $note = '履歴は消せませんでした。Win＋V を押し、「すべてクリア」を押してください。' }
    if ($note) { return New-Row 'clipboard' $name 'SKIP' '' '空' ('今のクリップボードは空にしました。' + $note) }
    return New-Row 'clipboard' $name 'DONE' '' '空' ''
}

# ---- Wi-Fi：教室の基準に無い接続先を消す（初回は基準を覚えるだけ）
function Get-WifiBaselinePath { return (Join-Path $env:LOCALAPPDATA 'ManabitoClassSetup\teacher_baseline_wifi.txt') }

function Get-WifiProfiles {
    $out = @(& netsh.exe wlan show profiles 2>$null)
    $names = @()
    foreach ($l in $out) {
        if ([string]$l -match '(プロファイル|Profile)\s*:\s*(.+)$') { $n = $Matches[2].Trim(); if ($n) { $names += $n } }
    }
    return ,$names
}

function Get-ConnectedSsid {
    foreach ($l in @(& netsh.exe wlan show interfaces 2>$null)) {
        if ([string]$l -match '^\s*SSID\s*:\s*(.+)$') { return $Matches[1].Trim() }
    }
    return ''
}

function Clear-ForeignWifi {
    param($Prof)
    $name = 'Wi-Fi の接続先（教室の基準に無いもの）'
    if (-not $env:LOCALAPPDATA) { return New-Row 'wifi' $name 'SKIP' '' '' '' }
    $all = Get-WifiProfiles
    $base = Get-WifiBaselinePath
    if (-not (Test-Path -LiteralPath $base)) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $base) -Force | Out-Null
        [IO.File]::WriteAllLines($base, [string[]]$all, (New-Object Text.UTF8Encoding $true))
        return New-Row 'wifi' $name 'DONE' ('{0} 件' -f $all.Count) '基準' ('初めての片付けなので、今の接続先（' + ($all -join '・') + '）を教室の基準として覚えました。次からは、これに無いものを消します。')
    }
    $keep = @(Get-Content -LiteralPath $base -Encoding UTF8 | ForEach-Object { ([string]$_).TrimStart([char]0xFEFF) } | Where-Object { $_ })
    $now = Get-ConnectedSsid
    $remove = @($all | Where-Object { $keep -notcontains $_ -and $_ -ne $now })
    if ($remove.Count -eq 0) { return New-Row 'wifi' $name 'SAME' '0 件' '0 件' '' }
    $failed = @()
    foreach ($n in $remove) {
        # 名前に空白があっても崩れないよう、netsh に渡す文字をそのまま組み立てる（PowerShell 5.1 は引用符を含む引数を崩すため）
        try {
            $p = Start-Process -FilePath 'netsh.exe' -ArgumentList ('wlan delete profile name="' + $n + '"') -Wait -PassThru -WindowStyle Hidden -ErrorAction Stop
            if ($p.ExitCode -ne 0) { $failed += $n }
        } catch { $failed += $n }
    }
    if ($failed.Count) { return New-Row 'wifi' $name 'FAIL' ('{0} 件' -f $remove.Count) '0 件' ('消せなかったもの：' + ($failed -join '・')) }
    return New-Row 'wifi' $name 'DONE' ('{0} 件' -f $remove.Count) '0 件' ('消したもの：' + ($remove -join '・'))
}

# まなびと講習準備 ― 共通の部品
# 判定の記号: OK=そろっている  NG=違う  PART=一部異なる  INFO=情報  NA=対象外  ERR=確認できない

$script:DetailLines = New-Object System.Collections.Generic.List[string]

function New-Row {
    param([string]$Id, [string]$Name, [string]$State,
          [string]$Current = '', [string]$Expected = '', [string]$Detail = '')
    [pscustomobject]@{
        Id = $Id; Name = $Name; State = $State
        Current = $Current; Expected = $Expected; Detail = $Detail
    }
}

# 詳しい記録（_詳細.txt）に書く行
function Add-Detail {
    param([string]$Text)
    $script:DetailLines.Add($Text) | Out-Null
}

# レジストリの値を読む。無ければ $null。
function Get-RegValue {
    param([string]$Path, [string]$Name)
    # 値が無いのは普通のことなので、エラーを出さない読み方にする（記録に「終了エラー」が並ばないように）
    $key = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
    if ($null -eq $key) { return $null }
    try { return $key.GetValue($Name, $null) } catch { return $null }
}

# 電池の有無（ノート PC かどうかの目安）
function Test-HasBattery {
    $b = @(Get-CimInstance -ClassName Win32_Battery -ErrorAction SilentlyContinue)
    return ($b.Count -gt 0)
}

# プロファイルの値を読む。無ければ既定値。
function Get-Pref {
    param($Prof, [string]$Name, $Default)
    if ($null -eq $Prof) { return $Default }
    $p = $Prof.PSObject.Properties[$Name]
    if ($null -eq $p -or $null -eq $p.Value) { return $Default }
    return $p.Value
}

function Format-Minutes {
    param([uint32]$Seconds)
    if ($Seconds -eq 0) { return 'しない' }
    if ($Seconds % 60 -eq 0) { return ('{0} 分後' -f ($Seconds / 60)) }
    return ('{0} 秒後' -f $Seconds)
}

function Format-Size {
    param([double]$Bytes)
    if ($Bytes -ge 1MB) { return ('{0:N1} MB' -f ($Bytes / 1MB)) }
    if ($Bytes -ge 1KB) { return ('{0:N0} KB' -f ($Bytes / 1KB)) }
    return ('{0:N0} バイト' -f $Bytes)
}

# フォルダーの中身を数える（desktop.ini は数えない）。フォルダーが無ければ -1。
function Get-ItemCount {
    param([string]$Path, [string]$Filter = '*')
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return -1 }
    $items = @(Get-ChildItem -LiteralPath $Path -Force -Filter $Filter -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -ne 'desktop.ini' })
    return $items.Count
}

# このアプリのフォルダーそのもの・その親・その中身なら $true（$AppPath が空なら $false）
function Test-IsAppFolder {
    param([string]$Path, [string]$AppPath)
    if (-not $AppPath -or -not $Path) { return $false }
    $p = $Path.TrimEnd('\', '/'); $g = $AppPath.TrimEnd('\', '/')
    if ($g.Equals($p, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    if ($g.StartsWith($p + '\', [StringComparison]::OrdinalIgnoreCase)) { return $true }
    return $p.StartsWith($g + '\', [StringComparison]::OrdinalIgnoreCase)
}

# Edge・Chrome の場所（利用者ごと）
function Get-BrowserList {
    return @(
        [pscustomobject]@{ Id = 'edge';   Name = 'Microsoft Edge';  Process = 'msedge'; Root = (Join-Path $env:LOCALAPPDATA 'Microsoft\Edge\User Data') },
        [pscustomobject]@{ Id = 'chrome'; Name = 'Google Chrome';   Process = 'chrome'; Root = (Join-Path $env:LOCALAPPDATA 'Google\Chrome\User Data') }
    )
}

# ブラウザーのプロファイルのフォルダー（Default、Profile 1…、Guest Profile）
function Get-BrowserProfiles {
    param([string]$Root)
    if (-not (Test-Path -LiteralPath $Root)) { return @() }
    return @(Get-ChildItem -LiteralPath $Root -Directory -Force -ErrorAction SilentlyContinue |
             Where-Object { $_.Name -eq 'Default' -or $_.Name -like 'Profile *' -or $_.Name -eq 'Guest Profile' })
}

# お気に入り（Bookmarks）の件数
function Get-BookmarkCount {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return 0 }
    try { $j = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json } catch { return -1 }
    $count = 0
    $stack = New-Object System.Collections.Stack
    foreach ($r in $j.roots.PSObject.Properties) { if ($r.Value -and $r.Value.PSObject.Properties['children']) { $stack.Push($r.Value) } }
    while ($stack.Count -gt 0) {
        $n = $stack.Pop()
        foreach ($c in @($n.children)) {
            if ($c.type -eq 'url') { $count++ }
            elseif ($c.PSObject.Properties['children']) { $stack.Push($c) }
        }
    }
    return $count
}

# ---- 作業の順番待ち（そろえる・履歴を消す・時間のかかる準備が、同時に 2 つ走らないようにする）
# PC 全体で 1 つの名前付きミューテックスを使う。プロセスが終われば自動で手放される。
$script:WorkLockHandle = $null

function Enter-WorkLock {
    param([int]$WaitSeconds = 0)
    try {
        if (-not $script:WorkLockHandle) {
            $script:WorkLockHandle = New-Object System.Threading.Mutex($false, 'Global\ManabitoClassSetupWork')
        }
        return $script:WorkLockHandle.WaitOne($WaitSeconds * 1000)
    } catch [System.Threading.AbandonedMutexException] {
        return $true   # 前の作業が異常終了して手放さなかった → 受け取れている
    } catch {
        Write-Host ('順番待ちの仕組みを使えませんでした: ' + $_.Exception.Message)
        return $true   # 仕組みが使えないときは止めない
    }
}

# 授業ファイルを受け取るフォルダー（アプリが %LOCALAPPDATA%\ManabitoClassSetup\share_dest.txt に覚えている。無ければドキュメント\授業ファイル）
function Get-ClassFilesPath {
    $file = Join-Path $env:LOCALAPPDATA 'ManabitoClassSetup\share_dest.txt'
    if (Test-Path -LiteralPath $file) {
        $p = (Get-Content -LiteralPath $file -Raw -Encoding UTF8).Trim()
        if ($p) { return $p }
    }
    return (Join-Path ([Environment]::GetFolderPath('MyDocuments')) '授業ファイル')
}

# 消してよい場所か（ドライブの直下・利用者のフォルダーそのもの・Windows や Program Files の中は消さない）
function Test-SafeClassFilesPath {
    param([string]$Path)
    if (-not $Path -or -not [System.IO.Path]::IsPathRooted($Path)) { return $false }
    $p = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    if ($p.Length -le 3) { return $false }
    $deny = @($env:USERPROFILE, [Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('MyDocuments'),
              (Join-Path $env:USERPROFILE 'Downloads'), $env:WINDIR, $env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramData)
    foreach ($d in $deny) {
        if (-not $d) { continue }
        $d = $d.TrimEnd('\', '/')
        if ($p -ieq $d) { return $false }
    }
    foreach ($d in @($env:WINDIR, $env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramData)) {
        if ($d -and $p.StartsWith($d.TrimEnd('\', '/') + '\', [StringComparison]::OrdinalIgnoreCase)) { return $false }
    }
    return $true
}

# 授業ファイルの受け取り先が、ダウンロード・デスクトップのフォルダーそのものか（それならその項目で点検・消去する）。同じなら項目の名前、違えば空
function Get-ClassFilesOverlap {
    param([string]$Path)
    if (-not $Path) { return '' }
    try { $full = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/') } catch { return '' }
    $dl = ''
    try {
        $v = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders' -ErrorAction Stop).'{374DE290-123F-4565-9164-39C4925E467B}'
        if ($v) { $dl = [Environment]::ExpandEnvironmentVariables([string]$v) }
    } catch { }
    if (-not $dl -and $env:USERPROFILE) { $dl = Join-Path $env:USERPROFILE 'Downloads' }
    if ($dl -and $full -ieq ([System.IO.Path]::GetFullPath($dl).TrimEnd('\', '/'))) { return 'ダウンロード フォルダー' }
    $desk = [Environment]::GetFolderPath('Desktop')
    if ($desk -and $full -ieq ([System.IO.Path]::GetFullPath($desk).TrimEnd('\', '/'))) { return 'デスクトップのファイル' }
    return ''
}

# ---- エクスプローラーのクイック アクセス（v0.19.7）
# 戻り値：@{ Ok（調べられたか）; Pinned（ピン留めの場所の一覧）; Others（ピン留めでない項目＝よく使うフォルダー・最近使ったファイル）}
# ピン留めかは、エクスプローラーが持つ印 System.Home.IsPinned で判断する。印を読めない項目が 1 つでもあれば Ok=$false（消す処理はしない）。
function Get-QuickAccessItems {
    $r = @{ Ok = $false; Pinned = @(); Others = @() }
    try {
        $sh = New-Object -ComObject Shell.Application
        $qa = $sh.Namespace('shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}')
        if (-not $qa) { return $r }
        foreach ($it in @($qa.Items())) {
            $pin = $it.ExtendedProperty('System.Home.IsPinned')
            if ($null -eq $pin) { return $r }   # 印を読めない：ピン留めを見分けられないので、何もしない
            if ([bool]$pin) { $r.Pinned += [string]$it.Path } else { $r.Others += $it }
        }
        $r.Ok = $true
    } catch { }
    return $r
}

# ---- Local（WordPress を PC の中で動かすアプリ）のサイト（v0.20.9）
function Get-LocalSitesPath {
    if (-not $env:USERPROFILE) { return '' }
    return (Join-Path $env:USERPROFILE 'Local Sites')
}

# Local の場所から動いている部品（Local 本体・MySQL・nginx・PHP など）を止める。名前だけで止めると、XAMPP など別のアプリの同じ名前の部品まで止めるため、場所で見分ける
function Stop-LocalWpProcesses {
    $roots = @()
    if ($env:APPDATA) { $roots += (Join-Path $env:APPDATA 'Local') }                  # サイトを動かす部品（lightning-services）
    if ($env:LOCALAPPDATA) { $roots += (Join-Path $env:LOCALAPPDATA 'Programs\Local') }
    foreach ($pf in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) { if ($pf) { $roots += (Join-Path $pf 'Local') } }
    $sites = Get-LocalSitesPath; if ($sites) { $roots += $sites }
    $stopped = 0
    foreach ($p in @(Get-Process -ErrorAction SilentlyContinue)) {
        $path = $null
        try { $path = $p.Path } catch { }
        if (-not $path) { continue }
        foreach ($r in $roots) {
            if ($path.StartsWith($r.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
                try { Stop-Process -Id $p.Id -Force -ErrorAction Stop; $stopped++ } catch { }
                break
            }
        }
    }
    if ($stopped -gt 0) { Start-Sleep -Seconds 2 }
    return $stopped
}

# ---- アプリの設定（前の受講生が変えたもの。v0.20.10）
# このツールで入れるアプリの設定のうち、前の受講生が変えると次の受講生が困るもの。戻り値：@{ App; Kind('file'|'dir'|'reg'); Path; Proc(開いていると消さない) } の一覧（あるものだけ）
function Get-AppSettingItems {
    $items = @()
    $R = $env:APPDATA; $L = $env:LOCALAPPDATA
    # Office：Word の標準テンプレート・リボンとクイック アクセス ツール バーの変更・Excel の起動時に開くファイル（消すと Office が初めの状態で作り直す）
    if ($R) {
        foreach ($n in @('Normal.dotm', 'NormalEmail.dotm')) {
            $f = Join-Path $R ('Microsoft\Templates\' + $n)
            if (Test-Path -LiteralPath $f) { $items += @{ App = 'Word'; Kind = 'file'; Path = $f; Proc = @('WINWORD', 'OUTLOOK') } }
        }
        $xs = Join-Path $R 'Microsoft\Excel\XLSTART'
        if (Test-Path -LiteralPath $xs) {
            foreach ($f in @(Get-ChildItem -LiteralPath $xs -File -Force -ErrorAction SilentlyContinue)) { $items += @{ App = 'Excel'; Kind = 'file'; Path = $f.FullName; Proc = @('EXCEL') } }
        }
    }
    if ($L) {
        $ui = Join-Path $L 'Microsoft\Office'
        if (Test-Path -LiteralPath $ui) {
            foreach ($f in @(Get-ChildItem -LiteralPath $ui -Filter '*.officeUI' -File -Force -ErrorAction SilentlyContinue)) {
                $items += @{ App = 'Office のリボン'; Kind = 'file'; Path = $f.FullName; Proc = @('WINWORD', 'EXCEL', 'POWERPNT', 'OUTLOOK') }
            }
        }
    }
    # Adobe CC のアプリの設定（Acrobat のフォルダーは残す）
    if ($R) {
        $ad = Join-Path $R 'Adobe'
        if (Test-Path -LiteralPath $ad) {
            foreach ($d in @(Get-ChildItem -LiteralPath $ad -Directory -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -notlike 'Acrobat*' })) {
                $items += @{ App = 'Adobe CC'; Kind = 'dir'; Path = $d.FullName; Proc = @('Photoshop', 'Illustrator', 'InDesign', 'AfterFX', 'Adobe Premiere Pro', 'Animate', 'Dreamweaver', 'Lightroom', 'Adobe Audition', 'Creative Cloud') }
            }
        }
    }
    # Acrobat Reader の最近使ったファイル（同意や初回の確認の設定は残す）
    foreach ($base in @('HKCU:\Software\Adobe\Acrobat Reader', 'HKCU:\Software\Adobe\Adobe Acrobat')) {
        if (-not (Test-Path -LiteralPath $base)) { continue }
        foreach ($ver in @(Get-ChildItem -LiteralPath $base -ErrorAction SilentlyContinue)) {
            foreach ($k in @('AVGeneral\cRecentFiles', 'AVGeneral\cRecentFolders')) {
                $kp = Join-Path $ver.PSPath $k
                if (Test-Path -LiteralPath $kp) { $items += @{ App = 'Acrobat Reader'; Kind = 'reg'; Path = $kp; Proc = @('Acrobat', 'AcroRd32') } }
            }
        }
    }
    # Paint.NET の設定（最近使ったファイルを含む）
    if (Test-Path -LiteralPath 'HKCU:\Software\paint.net') { $items += @{ App = 'Paint.NET'; Kind = 'reg'; Path = 'HKCU:\Software\paint.net'; Proc = @('paintdotnet', 'PaintDotNet') } }
    return ,$items
}

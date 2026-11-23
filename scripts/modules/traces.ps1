# まなびと講習準備 ― 前の受講生の痕跡の点検（見るだけ。何も消さない）

function Test-RecentFiles {
    param($Prof)
    $recent = [Environment]::GetFolderPath('Recent')
    $lnk = Get-ItemCount $recent '*.lnk'
    $auto = Get-ItemCount (Join-Path $recent 'AutomaticDestinations') '*.automaticDestinations-ms'
    $custom = Get-ItemCount (Join-Path $recent 'CustomDestinations') '*.customDestinations-ms'
    if ($lnk -lt 0) {
        return New-Row 'recent' '最近使ったファイル' 'ERR' '' '0 件' ('フォルダーが見つかりません: ' + $recent)
    }
    $detail = ('{0}: ショートカット {1} 件、ジャンプリスト（自動）{2} 件、（独自）{3} 件。' -f $recent, $lnk, [Math]::Max($auto, 0), [Math]::Max($custom, 0)) +
              ' ジャンプリスト（自動）にはクイックアクセスのピン留めも含まれます。'
    $state = 'NG'; if ($lnk -eq 0) { $state = 'OK' }
    return New-Row 'recent' '最近使ったファイル' $state ('{0} 件' -f $lnk) '0 件' $detail
}

function Test-ExplorerTyped {
    param($Prof)
    $typed = 0
    $tp = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\TypedPaths'
    if (Test-Path -LiteralPath $tp) {
        $typed = @((Get-Item -LiteralPath $tp).GetValueNames() | Where-Object { $_ -like 'url*' }).Count
    }
    $run = 0
    $rp = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\RunMRU'
    if (Test-Path -LiteralPath $rp) {
        $run = @((Get-Item -LiteralPath $rp).GetValueNames() | Where-Object { $_ -ne 'MRUList' -and $_ -ne '' }).Count
    }
    $total = $typed + $run
    $state = 'NG'; if ($total -eq 0) { $state = 'OK' }
    return New-Row 'typed' 'アドレス欄・「ファイル名を指定して実行」の入力履歴' $state ('アドレス欄 {0} 件 ／ 実行 {1} 件' -f $typed, $run) '0 件' ''
}

function Test-OfficeMru {
    param($Prof)
    $base = 'HKCU:\Software\Microsoft\Office\16.0'
    if (-not (Test-Path -LiteralPath $base)) {
        return New-Row 'officemru' 'Office の最近使ったファイル' 'NA' 'Office の設定なし' '0 件' 'この利用者で Office を使った記録がありません。'
    }
    $total = 0
    $parts = @()
    foreach ($app in @('Word', 'Excel', 'PowerPoint')) {
        $n = 0
        $um = Join-Path $base ($app + '\User MRU')
        if (Test-Path -LiteralPath $um) {
            foreach ($id in @(Get-ChildItem -LiteralPath $um -ErrorAction SilentlyContinue)) {
                $fm = Join-Path $id.PSPath 'File MRU'
                if (Test-Path -LiteralPath $fm) {
                    $n += @((Get-Item -LiteralPath $fm).GetValueNames() | Where-Object { $_ -like 'Item *' }).Count
                }
            }
        }
        $total += $n
        $parts += ('{0} {1}' -f $app, $n)
    }
    $state = 'NG'; if ($total -eq 0) { $state = 'OK' }
    return New-Row 'officemru' 'Office の最近使ったファイル' $state ($parts -join ' ／ ') '0 件' 'User MRU の File MRU を数えています。'
}

function Test-NotepadHistory {
    param($Prof)
    $pkg = Join-Path $env:LOCALAPPDATA 'Packages\Microsoft.WindowsNotepad_8wekyb3d8bbwe'
    if (-not (Test-Path -LiteralPath $pkg)) {
        return New-Row 'notepad' 'メモ帳の履歴（開いたままのタブ）' 'NA' '新しいメモ帳が無い' '0 件' ''
    }
    $tab = Join-Path $pkg 'LocalState\TabState'
    $n = Get-ItemCount $tab '*.bin'
    if ($n -lt 0) { $n = 0 }
    Add-Detail '--- メモ帳のフォルダー（LocalState） ---'
    foreach ($f in @(Get-ChildItem -LiteralPath (Join-Path $pkg 'LocalState') -Recurse -Force -File -ErrorAction SilentlyContinue)) {
        Add-Detail ('{0}  {1}  {2:yyyy/MM/dd HH:mm}' -f $f.FullName, (Format-Size $f.Length), $f.LastWriteTime)
    }
    $state = 'NG'; if ($n -eq 0) { $state = 'OK' }
    return New-Row 'notepad' 'メモ帳の履歴（開いたままのタブ）' $state ('{0} 件' -f $n) '0 件' ('TabState の .bin を数えています。ファイルの一覧は詳細の記録にあります。')
}

# IME の学習は「消えているか」を外から判定できないため、置き場所の洗い出しだけ行う
function Test-ImeLearning {
    param($Prof)
    $dirs = @(
        (Join-Path $env:APPDATA 'Microsoft\IME\15.0\IMEJP'),
        (Join-Path $env:LOCALAPPDATA 'Microsoft\IME\15.0\IMEJP'),
        (Join-Path $env:APPDATA 'Microsoft\InputMethod'),
        (Join-Path $env:LOCALAPPDATA 'Microsoft\InputMethod')
    )
    $count = 0; $size = 0
    Add-Detail '--- IME のファイル（学習・ユーザー辞書の候補） ---'
    foreach ($d in $dirs) {
        if (-not (Test-Path -LiteralPath $d)) { Add-Detail ('（なし）' + $d); continue }
        foreach ($f in @(Get-ChildItem -LiteralPath $d -Recurse -Force -File -ErrorAction SilentlyContinue)) {
            $count++; $size += $f.Length
            Add-Detail ('{0}  {1}  {2:yyyy/MM/dd HH:mm}' -f $f.FullName, (Format-Size $f.Length), $f.LastWriteTime)
        }
    }
    return New-Row 'ime' '日本語入力（IME）の学習' 'INFO' ('関係しそうなファイル {0} 個（{1}）' -f $count, (Format-Size $size)) '学習なし' '消せたかどうかを外から判定できないため、今は置き場所を洗い出しています。一覧は詳細の記録にあります。'
}

function Get-DownloadsPath {
    $p = Get-RegValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders' '{374DE290-123F-4565-9164-39C4925E467B}'
    if ($p) { return [Environment]::ExpandEnvironmentVariables([string]$p) }
    return (Join-Path $env:USERPROFILE 'Downloads')
}

$script:InspectAppPath = ''   # このアプリのフォルダー（inspect.ps1 が -AppPath から設定する）

# 残っているのがこのアプリのフォルダーだけなら PART（一部異なる）にする
function Get-LeftoverState {
    param($Items, [string]$OkWhenEmpty)
    $mine = @($Items | Where-Object { Test-IsAppFolder $_.FullName $script:InspectAppPath })
    $other = @($Items | Where-Object { -not (Test-IsAppFolder $_.FullName $script:InspectAppPath) })
    $r = [pscustomobject]@{ State = 'NG'; Mine = $mine; Other = $other }
    if ($Items.Count -eq 0) { $r.State = 'OK' }
    elseif ($other.Count -eq 0) { $r.State = 'PART' }
    return $r
}

function Test-Downloads {
    param($Prof)
    $p = Get-DownloadsPath
    if (-not (Test-Path -LiteralPath $p)) {
        return New-Row 'downloads' 'ダウンロード フォルダー' 'ERR' '' '空' ('フォルダーが見つかりません: ' + $p)
    }
    $items = @(Get-ChildItem -LiteralPath $p -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'desktop.ini' })
    $r = Get-LeftoverState $items
    if ($r.State -eq 'PART') {
        return New-Row 'downloads' 'ダウンロード フォルダー' 'PART' ('このアプリのフォルダーだけ（' + (($r.Mine | ForEach-Object { $_.Name }) -join '、') + '）') '空' ($p + ' ※ このアプリが入っているフォルダーは、［履歴を消す］でも残します。')
    }
    $cur = ('{0} 個' -f $items.Count)
    if ($r.Mine.Count -gt 0) { $cur += ('（うちこのアプリのフォルダー {0} 個）' -f $r.Mine.Count) }
    return New-Row 'downloads' 'ダウンロード フォルダー' $r.State $cur '空' $p
}

function Test-DesktopFiles {
    param($Prof)
    $p = [Environment]::GetFolderPath('Desktop')
    if (-not (Test-Path -LiteralPath $p)) {
        return New-Row 'desktop' 'デスクトップのファイル' 'ERR' '' 'ショートカットだけ' ('フォルダーが見つかりません: ' + $p)
    }
    $items = @(Get-ChildItem -LiteralPath $p -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'desktop.ini' })
    $links = @($items | Where-Object { $_.Extension -in @('.lnk', '.url') }).Count
    $files = @($items | Where-Object { $_.Extension -notin @('.lnk', '.url') })
    $other = $files.Count
    $r = Get-LeftoverState $files
    $state = $r.State
    $detail = $p
    if ($state -eq 'PART') {
        return New-Row 'desktop' 'デスクトップのファイル' 'PART' ('このアプリのフォルダーだけ（' + (($r.Mine | ForEach-Object { $_.Name }) -join '、') + '）／ ショートカット ' + $links + ' 個') 'ショートカットだけ' ($p + ' ※ このアプリが入っているフォルダーは、［履歴を消す］でも残します。')
    }
    if ($p -match 'OneDrive') {
        $detail += ' ※ デスクトップが OneDrive と同期しています。ここで消すとクラウド側からも消えるため、片付けの対象から外します。'
    }
    return New-Row 'desktop' 'デスクトップのファイル' $state ('ファイル・フォルダー {0} 個 ／ ショートカット {1} 個' -f $other, $links) 'ショートカットだけ' $detail
}

function Test-RecycleBin {
    param($Prof)
    $sh = New-Object -ComObject Shell.Application
    $n = $sh.Namespace(10).Items().Count
    $state = 'NG'; if ($n -eq 0) { $state = 'OK' }
    return New-Row 'recycle' 'ごみ箱' $state ('{0} 個' -f $n) '空' ''
}

# Edge・Chrome：お気に入りの件数と、履歴・Cookie のファイルがあるか
function Test-BrowserData {
    param($Prof)
    $rows = @()
    foreach ($b in Get-BrowserList) {
        $id = 'browser-' + $b.Id
        $name = $b.Name + ' のお気に入り・履歴・Cookie'
        $profiles = Get-BrowserProfiles $b.Root
        if ($profiles.Count -eq 0) { $rows += New-Row $id $name 'NA' '使った記録なし' 'なし' $b.Root; continue }
        $marks = 0; $hist = 0; $cook = 0; $pw = 0
        foreach ($pf in $profiles) {
            $c = Get-BookmarkCount (Join-Path $pf.FullName 'Bookmarks'); if ($c -gt 0) { $marks += $c }
            if (Test-Path -LiteralPath (Join-Path $pf.FullName 'History')) { $hist++ }
            if ((Test-Path -LiteralPath (Join-Path $pf.FullName 'Network\Cookies')) -or (Test-Path -LiteralPath (Join-Path $pf.FullName 'Cookies'))) { $cook++ }
            if (Test-Path -LiteralPath (Join-Path $pf.FullName 'Login Data')) { $pw++ }
        }
        # ブラウザーへのサインイン（Local State の一覧にアカウント名が残る）と、拡張機能
        $signed = 0; $ext = 0
        try {
            $ls = Get-Content -LiteralPath (Join-Path $b.Root 'Local State') -Raw -Encoding UTF8 -ErrorAction Stop | ConvertFrom-Json
            foreach ($pp in $ls.profile.info_cache.PSObject.Properties) { if ($pp.Value.user_name) { $signed++ } }
        } catch { }
        foreach ($pf in $profiles) {
            $ed = Join-Path $pf.FullName 'Extensions'
            if (Test-Path -LiteralPath $ed) { $ext += @(Get-ChildItem -LiteralPath $ed -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'Temp' }).Count }
        }
        $parts = @(('お気に入り {0} 件' -f $marks))
        if ($signed) { $parts += ('ブラウザーにサインイン {0} 件' -f $signed) }
        if ($profiles.Count -gt 1) { $parts += ('プロファイル {0} 個' -f $profiles.Count) }
        if ($ext) { $parts += ('拡張機能 {0} 個' -f $ext) }
        if ($hist) { $parts += '履歴あり' }
        if ($cook) { $parts += 'Cookie あり' }
        if ($pw) { $parts += 'パスワード保存の記録あり' }
        $state = 'OK'; if ($marks -gt 0 -or $hist -or $cook -or $pw -or $signed -or $profiles.Count -gt 1) { $state = 'NG' }
        $rows += New-Row $id $name $state ($parts -join '・') 'なし' ('プロファイル ' + $profiles.Count + ' 個。ブラウザーを一度開くと空のファイルができるため、［履歴を消す］のあとに開くと「違う」になります。')
    }
    return $rows
}

function Test-ClassFiles {
    param($Prof)
    $p = Get-ClassFilesPath
    $name = '受け取った授業ファイル'
    $same = Get-ClassFilesOverlap $p
    if ($same) { return New-Row 'classfiles' $name 'NA' ('受け取る場所：' + $p) '空' ('受け取る場所が「' + $same + '」と同じため、その項目で点検・消去します。') }
    if (-not (Test-Path -LiteralPath $p)) { return New-Row 'classfiles' $name 'OK' 'なし' '空' $p }
    $n = @(Get-ChildItem -LiteralPath $p -Recurse -Force -File -ErrorAction SilentlyContinue).Count
    $state = 'NG'; if ($n -eq 0) { $state = 'OK' }
    return New-Row 'classfiles' $name $state ('{0} 個' -f $n) '空' ($p + ' ※ 前の講習の授業ファイル。［履歴を消す］で消します。')
}

function Test-QuickAccessFrequent {
    param($Prof)
    $name = 'エクスプローラーのよく使うフォルダー（クイック アクセス）'
    $q = Get-QuickAccessItems
    if (-not $q.Ok) { return New-Row 'quickaccess' $name 'NA' '調べられません' '0 件' 'この Windows では、ピン留めとよく使うフォルダーを見分けられませんでした。' }
    $n = @($q.Others).Count
    $state = 'OK'; if ($n -gt 0) { $state = 'NG' }
    $names = (@($q.Others) | Select-Object -First 5 | ForEach-Object { $_.Name }) -join '・'
    return New-Row 'quickaccess' $name $state ('{0} 件' -f $n) '0 件' ('ピン留めは数えていません（' + @($q.Pinned).Count + ' 個）。' + $(if ($names) { '例：' + $names } else { '' }))
}

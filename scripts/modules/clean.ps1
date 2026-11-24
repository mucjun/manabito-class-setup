# まなびと講習準備 ― 前の受講生の履歴を消す（消したものは元に戻せない）
#
# 守るもの：
#   ・このアプリが入っているフォルダー（とその親フォルダー）は消さない
#   ・OneDrive と同期しているデスクトップには手を付けない（クラウド側からも消えるため）
#   ・クイック アクセスのピン留め（f01b4d95cf55d32a.automaticDestinations-ms）は残す
#
# 各関数は 1 行の結果を返す。State は DONE / SAME / SKIP / FAIL。
#
# 注意：ここで $script: に置く変数の名前は、呼び出す側のスクリプトの引数名（-ProtectPath など）と
# 重ねないこと。同じ場所に入るため、読み込んだときに引数の値を消してしまう（v0.4.0 の不具合）。

$script:GuardPath = ''

$script:MoveFileCode = @'
using System;
using System.Runtime.InteropServices;
namespace ManabitoFiles {
    public static class Native {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern bool MoveFileEx(string existing, string newName, int flags);
    }
}
'@

# このアプリのフォルダーそのもの、またはその親なら $true
function Test-IsProtected {
    param([string]$Path)
    if (-not $script:GuardPath) { return $true }   # 場所が分からなければ、何も消さない側に倒す
    $p = $Path.TrimEnd('\')
    $guard = $script:GuardPath.TrimEnd('\')
    if ($guard.Equals($p, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    # 親フォルダー（アプリがこの中にある）
    if ($guard.StartsWith($p + '\', [StringComparison]::OrdinalIgnoreCase)) { return $true }
    # 子（アプリのフォルダーがダウンロードやデスクトップそのものだった場合の、その中身）
    return $p.StartsWith($guard + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Test-ProcessRunning {
    param([string[]]$Names)
    $sid = (Get-Process -Id $PID).SessionId
    $found = @(Get-Process -Name $Names -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sid })
    return ($found.Count -gt 0)
}

# 消す。消せなかった数を返す。
# 消す。エクスプローラーのプレビューや Defender の検査がファイルをつかんでいると、Windows は「消す予約」だけを入れ、
# 少しあとに消す。そのときフォルダーごと消すと「空ではない」とエラーになるが、実際には少しあとに消える（v0.19.0 の実機で起きた）。
# そこで、エラーのときは少し待ってやり直し（3 回まで）、最後に本当に残っているかを確かめてから「消せなかった」と数える。
function Remove-Items {
    param($Items)
    $failed = 0
    foreach ($it in @($Items)) {
        $lastError = ''
        for ($try = 1; $try -le 3; $try++) {
            try {
                if (Test-Path -LiteralPath $it.FullName) { Remove-Item -LiteralPath $it.FullName -Recurse -Force -ErrorAction Stop }
                $lastError = ''
                break
            } catch {
                $lastError = $_.Exception.Message
                Start-Sleep -Milliseconds (500 * $try)
            }
        }
        if ($lastError -and (Test-Path -LiteralPath $it.FullName)) {
            Start-Sleep -Seconds 2   # 「消す予約」が終わるのを待ってから、最後に確かめる
        }
        if (Test-Path -LiteralPath $it.FullName) {
            $failed++
            Add-Detail ('消せなかったもの: ' + $it.FullName + ' … ' + $lastError)
        } elseif ($lastError) {
            Add-Detail ('少し待ってから消えたもの: ' + $it.FullName)
        }
    }
    return $failed
}

function Format-CleanResult {
    param([string]$Id, [string]$Name, [int]$Total, [int]$Failed, [string]$Note = '')
    if ($Total -eq 0) { return New-Row $Id $Name 'SAME' '0 件' '0 件' $Note }
    $done = $Total - $Failed
    if ($Failed -gt 0) {
        return New-Row $Id $Name 'FAIL' ('{0} 件' -f $Total) ('{0} 件を消し、{1} 件は消せませんでした' -f $done, $Failed) $Note
    }
    return New-Row $Id $Name 'DONE' ('{0} 件' -f $Total) '0 件' $Note
}

# ---------------------------------------------------------------- 最近使ったファイル

function Clear-RecentFiles {
    param($Prof)
    $recent = [Environment]::GetFolderPath('Recent')
    $items = @()
    $items += @(Get-ChildItem -LiteralPath $recent -Force -File -Filter '*.lnk' -ErrorAction SilentlyContinue)
    $auto = Join-Path $recent 'AutomaticDestinations'
    $items += @(Get-ChildItem -LiteralPath $auto -Force -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -notlike 'f01b4d95cf55d32a*' })
    $items += @(Get-ChildItem -LiteralPath (Join-Path $recent 'CustomDestinations') -Force -File -ErrorAction SilentlyContinue)
    $failed = Remove-Items $items
    return Format-CleanResult 'recent' '最近使ったファイル（ジャンプリストを含む）' $items.Count $failed 'クイック アクセスのピン留めは残しています。'
}

# スタートの「最近使用」：最近使ったファイル（Clear-RecentFiles）を消したあと、スタートメニューを読み込み直させる
function Clear-StartMenuRecent {
    param($Prof)
    $name = 'スタートの「最近使用」'
    $sid = (Get-Process -Id $PID).SessionId
    $hosts = @(Get-Process -Name 'StartMenuExperienceHost' -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sid })
    foreach ($h in $hosts) { Stop-Process -Id $h.Id -Force -ErrorAction SilentlyContinue }   # Windows がすぐに起動し直す
    if ($hosts.Count -eq 0) { return New-Row 'startrecent-clean' $name 'SAME' '' '空' '' }
    return New-Row 'startrecent-clean' $name 'DONE' '' '空' 'スタートメニューを読み込み直しました（最近使ったファイルは前の項目で消しています）。'
}

function Clear-ExplorerTyped {
    param($Prof)
    $n = 0
    $tp = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\TypedPaths'
    if (Test-Path -LiteralPath $tp) {
        foreach ($v in @((Get-Item -LiteralPath $tp).GetValueNames() | Where-Object { $_ -like 'url*' })) {
            Remove-ItemProperty -LiteralPath $tp -Name $v -Force -ErrorAction Stop; $n++
        }
    }
    $rp = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\RunMRU'
    if (Test-Path -LiteralPath $rp) {
        foreach ($v in @((Get-Item -LiteralPath $rp).GetValueNames() | Where-Object { $_ -ne '' })) {
            Remove-ItemProperty -LiteralPath $rp -Name $v -Force -ErrorAction Stop
            if ($v -ne 'MRUList') { $n++ }
        }
    }
    if ($n -eq 0) { return New-Row 'typed' 'アドレス欄・「ファイル名を指定して実行」の入力履歴' 'SAME' '0 件' '0 件' '' }
    return New-Row 'typed' 'アドレス欄・「ファイル名を指定して実行」の入力履歴' 'DONE' ('{0} 件' -f $n) '0 件' ''
}

function Clear-OfficeMru {
    param($Prof)
    $name = 'Office の最近使ったファイル'
    $base = 'HKCU:\Software\Microsoft\Office\16.0'
    if (-not (Test-Path -LiteralPath $base)) { return New-Row 'officemru' $name 'SKIP' 'Office の設定なし' '0 件' '' }
    if (Test-ProcessRunning @('WINWORD', 'EXCEL', 'POWERPNT')) {
        return New-Row 'officemru' $name 'FAIL' '' '0 件' 'Word・Excel・PowerPoint のどれかが開いています。閉じてからもう一度［履歴を消す］を押してください（開いたまま消しても、閉じるときに書き戻されます）。'
    }
    $n = 0
    foreach ($app in @('Word', 'Excel', 'PowerPoint')) {
        $um = Join-Path $base ($app + '\User MRU')
        if (-not (Test-Path -LiteralPath $um)) { continue }
        foreach ($id in @(Get-ChildItem -LiteralPath $um -ErrorAction SilentlyContinue)) {
            foreach ($sub in @('File MRU', 'Place MRU')) {
                $k = Join-Path $id.PSPath $sub
                if (-not (Test-Path -LiteralPath $k)) { continue }
                foreach ($v in @((Get-Item -LiteralPath $k).GetValueNames() | Where-Object { $_ -like 'Item *' })) {
                    Remove-ItemProperty -LiteralPath $k -Name $v -Force -ErrorAction Stop
                    if ($sub -eq 'File MRU') { $n++ }
                }
            }
        }
    }
    if ($n -eq 0) { return New-Row 'officemru' $name 'SAME' '0 件' '0 件' '' }
    return New-Row 'officemru' $name 'DONE' ('{0} 件' -f $n) '0 件' '最近使ったフォルダー（Place MRU）も消しました。'
}

function Clear-NotepadHistory {
    param($Prof)
    $name = 'メモ帳の履歴（開いたままのタブ）'
    $pkg = Join-Path $env:LOCALAPPDATA 'Packages\Microsoft.WindowsNotepad_8wekyb3d8bbwe\LocalState'
    if (-not (Test-Path -LiteralPath $pkg)) { return New-Row 'notepad' $name 'SKIP' '新しいメモ帳が無い' '0 件' '' }
    if (Test-ProcessRunning @('Notepad')) {
        return New-Row 'notepad' $name 'FAIL' '' '0 件' 'メモ帳が開いています。閉じてからもう一度［履歴を消す］を押してください。'
    }
    $items = @()
    foreach ($sub in @('TabState', 'WindowState')) {
        $d = Join-Path $pkg $sub
        $items += @(Get-ChildItem -LiteralPath $d -Force -File -ErrorAction SilentlyContinue)
    }
    $failed = Remove-Items $items
    return Format-CleanResult 'notepad' $name $items.Count $failed ''
}

# ---------------------------------------------------------------- IME（学習・入力履歴・ユーザー辞書）

function Clear-ImeData {
    param($Prof)
    $name = '日本語入力（IME）の学習とユーザー辞書'
    $files = @(
        (Join-Path $env:APPDATA 'Microsoft\IME\15.0\IMEJP\UserDict\imjp15cu.dic'),
        (Join-Path $env:APPDATA 'Microsoft\IME\15.0\IMEJP\UserDict\imjp15cu.dic_bak'),
        (Join-Path $env:LOCALAPPDATA 'Microsoft\IME\15.0\IMEJP\Cache\imjp15cache.dat'),
        (Join-Path $env:LOCALAPPDATA 'Microsoft\IME\15.0\IMEJP\Cache\imjp15cache.dat.back'),
        (Join-Path $env:APPDATA 'Microsoft\InputMethod\Shared\JpnIHDS.dat')
    )
    $present = @($files | Where-Object { Test-Path -LiteralPath $_ })
    if ($present.Count -eq 0) { return New-Row 'ime' $name 'SAME' 'ファイルなし' '学習なし' '' }
    $now = 0; $later = 0; $failed = 0
    foreach ($f in $present) {
        try {
            Remove-Item -LiteralPath $f -Force -ErrorAction Stop
            $now++
            Add-Detail ('消した: ' + $f)
        } catch {
            # IME が使っていて今は消せない → 次に PC を起動したときに消す（管理者が必要）
            if (-not ('ManabitoFiles.Native' -as [type])) { Add-Type -TypeDefinition $script:MoveFileCode -Language CSharp }
            if ([ManabitoFiles.Native]::MoveFileEx($f, [NullString]::Value, 4)) {   # MOVEFILE_DELAY_UNTIL_REBOOT
                $later++
                Add-Detail ('再起動のときに消す: ' + $f)
            } else {
                $failed++
                Add-Detail ('消せなかった: ' + $f + ' … ' + $_.Exception.Message)
            }
        }
    }
    $cur = ('ファイル {0} 個' -f $present.Count)
    if ($failed -gt 0) {
        return New-Row 'ime' $name 'FAIL' $cur '学習なし' ('{0} 個は消せませんでした。詳しくは記録を見てください。' -f $failed)
    }
    $note = '登録した単語も消えます。'
    if ($later -gt 0) { $note = ('{0} 個は IME が使っているため、次に PC を再起動したときに消えます。' -f $later) + $note }
    return New-Row 'ime' $name 'DONE' $cur '学習なし' $note
}

# ---------------------------------------------------------------- フォルダー

function Get-DownloadsPathForClean {
    $p = Get-RegValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders' '{374DE290-123F-4565-9164-39C4925E467B}'
    if ($p) { return [Environment]::ExpandEnvironmentVariables([string]$p) }
    return (Join-Path $env:USERPROFILE 'Downloads')
}

# アプリの場所が分からないときに返す行（ダウンロード・デスクトップ用）
function New-GuardUnknownRow {
    param([string]$Id, [string]$Name, [string]$Expected)
    return New-Row $Id $Name 'FAIL' '' $Expected 'このアプリの場所が分からないため、アプリごと消してしまわないよう、ここは消していません。'
}

function Clear-Downloads {
    param($Prof)
    $name = 'ダウンロード フォルダー'
    if (-not $script:GuardPath) { return New-GuardUnknownRow 'downloads' $name '空' }
    $p = Get-DownloadsPathForClean
    if (-not (Test-Path -LiteralPath $p)) { return New-Row 'downloads' $name 'SKIP' '' '空' ('フォルダーが見つかりません: ' + $p) }
    if ($p -match 'OneDrive') { return New-Row 'downloads' $name 'SKIP' '' '空' 'OneDrive と同期しているため、手を付けていません。' }
    $all = @(Get-ChildItem -LiteralPath $p -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'desktop.ini' })
    $keep = @($all | Where-Object { Test-IsProtected $_.FullName })
    $items = @($all | Where-Object { -not (Test-IsProtected $_.FullName) })
    $failed = Remove-Items $items
    $note = ''
    if ($keep.Count -gt 0) { $note = ('このアプリが入っているフォルダー（{0}）は残しました。' -f (($keep | ForEach-Object { $_.Name }) -join '、')) }
    if ($failed -gt 0) {
        # 別の場所に置いた講習準備アプリ（古い版など）が起動したままだと、その EXE は消せない
        $others = @(Get-Process -Name 'ManabitoClassSetup' -ErrorAction SilentlyContinue |
                    Where-Object { $_.Path -and $_.Path.StartsWith($p, [StringComparison]::OrdinalIgnoreCase) -and -not (Test-IsProtected $_.Path) })
        foreach ($o in $others) { $note += ('別の講習準備アプリ（' + $o.Path + '）が起動したままです。閉じてから、もう一度［履歴を消す］を押してください。') }
    }
    return Format-CleanResult 'downloads' $name $items.Count $failed $note
}

function Clear-DesktopFiles {
    param($Prof)
    $name = 'デスクトップのファイル'
    if (-not $script:GuardPath) { return New-GuardUnknownRow 'desktop' $name 'ショートカットだけ' }
    $p = [Environment]::GetFolderPath('Desktop')
    if (-not $p -or -not (Test-Path -LiteralPath $p)) { return New-Row 'desktop' $name 'SKIP' '' 'ショートカットだけ' 'フォルダーが見つかりません。' }
    if ($p -match 'OneDrive') { return New-Row 'desktop' $name 'SKIP' '' 'ショートカットだけ' 'OneDrive と同期しているため、手を付けていません。' }
    $all = @(Get-ChildItem -LiteralPath $p -Force -ErrorAction SilentlyContinue |
             Where-Object { $_.Name -ne 'desktop.ini' -and $_.Extension -notin @('.lnk', '.url') })
    $keep = @($all | Where-Object { Test-IsProtected $_.FullName })
    $items = @($all | Where-Object { -not (Test-IsProtected $_.FullName) })
    $failed = Remove-Items $items
    $note = 'ショートカットは残しています。'
    if ($keep.Count -gt 0) { $note += ('このアプリが入っているフォルダー（{0}）も残しました。' -f (($keep | ForEach-Object { $_.Name }) -join '、')) }
    return Format-CleanResult 'desktop' $name $items.Count $failed $note
}

function Clear-UserRecycleBin {
    param($Prof)
    $name = 'ごみ箱'
    $sh = New-Object -ComObject Shell.Application
    $n = $sh.Namespace(10).Items().Count
    if ($n -eq 0) { return New-Row 'recycle' $name 'SAME' '0 個' '空' '' }
    Clear-RecycleBin -Force -ErrorAction Stop
    $left = $sh.Namespace(10).Items().Count
    if ($left -gt 0) { return New-Row 'recycle' $name 'FAIL' ('{0} 個' -f $n) '空' ('{0} 個が残りました。' -f $left) }
    return New-Row 'recycle' $name 'DONE' ('{0} 個' -f $n) '空' ''
}

# ---------------------------------------------------------------- Edge・Chrome

# 消すもの（プロファイルのフォルダーの中の名前）。開いていると消せないので、先にブラウザーを閉じる。
$script:BrowserBaseItems = @(
    'History', 'History-journal', 'Visited Links', 'Top Sites', 'Top Sites-journal', 'Shortcuts', 'Shortcuts-journal',
    'Favicons', 'Favicons-journal', 'Sessions', 'Current Session', 'Current Tabs', 'Last Session', 'Last Tabs',
    'Bookmarks', 'Bookmarks.bak', 'Bookmarks.msbak',
    'Cookies', 'Cookies-journal', 'Network\Cookies', 'Network\Cookies-journal',
    'Local Storage', 'Session Storage', 'IndexedDB', 'Cache', 'Code Cache'
)
$script:BrowserPasswordItems = @(
    'Login Data', 'Login Data-journal', 'Login Data For Account', 'Login Data For Account-journal', 'Web Data', 'Web Data-journal'
)

function Stop-Browser {
    param([string]$ProcessName)
    $sid = (Get-Process -Id $PID).SessionId
    for ($i = 0; $i -lt 3; $i++) {
        $ps = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sid })
        if ($ps.Count -eq 0) { return }
        foreach ($p in $ps) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
        Start-Sleep -Seconds 2
    }
}

function Clear-BrowserData {
    param($Prof)
    $withPw = [bool](Get-Pref $Prof 'browserClearPasswords' $true)
    # 初めて開いたときの状態に戻す（User Data ごと）：ブラウザーへのサインイン・受講生が足したプロファイル・拡張機能も消える
    $fullReset = [bool](Get-Pref $Prof 'browserFullReset' $true)
    $names = @($script:BrowserBaseItems)
    if ($withPw) { $names += $script:BrowserPasswordItems }
    $rows = @()
    foreach ($b in Get-BrowserList) {
        $id = 'browser-' + $b.Id
        $label = $b.Name + ' のお気に入り・履歴・Cookie'
        if ($fullReset) { $label = $b.Name + ' のサインイン・お気に入り・履歴・拡張機能' }
        $profiles = Get-BrowserProfiles $b.Root
        if ($profiles.Count -eq 0 -and -not (Test-Path -LiteralPath $b.Root)) { $rows += New-Row $id $label 'SKIP' '使った記録なし' 'なし' ''; continue }
        Stop-Browser $b.Process
        if ($fullReset) {
            $items = @(Get-ChildItem -LiteralPath $b.Root -Force -ErrorAction SilentlyContinue)
            $failed = Remove-Items $items
            if ($failed -gt 0) {
                Stop-Browser $b.Process   # Edge は閉じてもすぐ裏で起動し直すことがある
                $failed = Remove-Items @($items | Where-Object { Test-Path -LiteralPath $_.FullName })
            }
            $note = ('プロファイル {0} 個。ブラウザーを閉じ、初めて開いたときの状態に戻しました（サインイン・拡張機能・保存されたパスワードも消えています）。次に開くと最初の設定の画面が出ます。' -f $profiles.Count)
            $rows += Format-CleanResult $id $label $items.Count $failed $note
            continue
        }
        $items = @()
        foreach ($pf in $profiles) {
            foreach ($n in $names) {
                $path = Join-Path $pf.FullName $n
                if (Test-Path -LiteralPath $path) { $items += Get-Item -LiteralPath $path -Force }
            }
        }
        $failed = Remove-Items $items
        if ($failed -gt 0) {
            # Edge は閉じてもすぐ裏で起動し直すことがあるので、もう一度閉じてから残りを消す
            Stop-Browser $b.Process
            $left = @($items | Where-Object { Test-Path -LiteralPath $_.FullName })
            $failed = Remove-Items $left
        }
        $note = ('プロファイル {0} 個。ブラウザーは閉じました。' -f $profiles.Count)
        if ($withPw) { $note += '保存されたパスワードと自動入力も消しました。' }
        $rows += Format-CleanResult $id $label $items.Count $failed $note
    }
    return $rows
}

# ---------------------------------------------------------------- 受け取った授業ファイル（前の講習の分）

function Clear-ClassFiles {
    param($Prof)
    $name = '受け取った授業ファイル'
    $p = Get-ClassFilesPath
    $same = Get-ClassFilesOverlap $p
    if ($same) { return New-Row 'classfiles' $name 'SKIP' '' '空' ('受け取る場所（' + $p + '）が「' + $same + '」と同じため、その項目で消しています。') }
    if (-not (Test-Path -LiteralPath $p)) { return New-Row 'classfiles' $name 'SAME' 'なし' '空' '' }
    if (-not (Test-SafeClassFilesPath $p)) {
        return New-Row 'classfiles' $name 'FAIL' '' '空' ('受け取る場所（' + $p + '）が、ドライブの直下や利用者のフォルダーそのものなどのため、念のため消していません。')
    }
    if (Test-IsProtected $p) { return New-Row 'classfiles' $name 'FAIL' '' '空' 'このアプリのフォルダーを含むため、消していません。' }
    $items = @(Get-ChildItem -LiteralPath $p -Force -ErrorAction SilentlyContinue)
    $failed = Remove-Items $items
    return Format-CleanResult 'classfiles' $name $items.Count $failed $p
}

# ---------------------------------------------------------------- エクスプローラーのよく使うフォルダー（v0.19.7）
# ピン留めでない項目だけを「クイック アクセスから削除」（removefromhome）で消す。ピン留めは残す。
# それで残ったときの予備：ピン留めが Windows の初めの 6 つ（デスクトップ・ダウンロード・ドキュメント・ピクチャ・ミュージック・ビデオ）だけなら、
# 記録のファイル（f01b4d95cf55d32a.automaticDestinations-ms）を消す。Windows が初めの 6 つを作り直すので、結果は同じになる。
function Clear-QuickAccessFrequent {
    param($Prof)
    $name = 'エクスプローラーのよく使うフォルダー（クイック アクセス）'
    $q = Get-QuickAccessItems
    if (-not $q.Ok) { return New-Row 'quickaccess' $name 'SKIP' '調べられません' '0 件' 'この Windows では、ピン留めとよく使うフォルダーを見分けられないため、何も消していません（ピン留めを守るため）。' }
    $before = @($q.Others).Count
    if ($before -eq 0) { return New-Row 'quickaccess' $name 'SAME' '0 件' '0 件' ('ピン留めは残しています（' + @($q.Pinned).Count + ' 個）。') }
    foreach ($it in @($q.Others)) {
        try { $it.InvokeVerb('removefromhome') } catch { }
    }
    Start-Sleep -Milliseconds 800
    $after = Get-QuickAccessItems
    $left = if ($after.Ok) { @($after.Others).Count } else { $before }
    $note = ''
    if ($left -gt 0) {
        # 予備：ピン留めが初めの 6 つだけなら、記録のファイルごと消す
        $defaults = @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('MyDocuments'), [Environment]::GetFolderPath('MyPictures'),
                      [Environment]::GetFolderPath('MyMusic'), [Environment]::GetFolderPath('MyVideos'))
        if ($env:USERPROFILE) { $defaults += (Join-Path $env:USERPROFILE 'Downloads') }
        try {
            $dl = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders' -ErrorAction Stop).'{374DE290-123F-4565-9164-39C4925E467B}'
            if ($dl) { $defaults += [Environment]::ExpandEnvironmentVariables([string]$dl) }
        } catch { }
        $onlyDefaults = $true
        foreach ($p in @($q.Pinned)) { if (-not ($defaults | Where-Object { $_ -and $_ -ieq $p })) { $onlyDefaults = $false } }
        $file = Join-Path $env:APPDATA 'Microsoft\Windows\Recent\AutomaticDestinations\f01b4d95cf55d32a.automaticDestinations-ms'
        if ($onlyDefaults -and (Test-Path -LiteralPath $file)) {
            # エクスプローラーが覚えている内容で書き戻さないよう、止めてから消し、また起動する
            Get-Process explorer -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
            Start-Sleep -Milliseconds 800
            Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
            Start-Process explorer.exe
            Start-Sleep -Seconds 2
            $again = Get-QuickAccessItems
            $left = if ($again.Ok) { @($again.Others).Count } else { $left }
            $note = '記録のファイルを消して、Windows の初めのピン留め（6 つ）に戻しました。'
        } elseif (-not $onlyDefaults) {
            $note = '初めのものではないピン留めがあるため、記録のファイルは消していません（ピン留めを守るため）。'
        }
    }
    if ($left -gt 0) {
        return New-Row 'quickaccess' $name 'FAIL' ('{0} 件' -f $before) '0 件' ('{0} 件が残りました。{1}エクスプローラーで右クリック →「クイック アクセスから削除」で消せます。' -f $left, $note)
    }
    return New-Row 'quickaccess' $name 'DONE' ('{0} 件' -f $before) '0 件' ('ピン留めは残しています（' + @($q.Pinned).Count + ' 個）。' + $note)
}

# ---------------------------------------------------------------- Local のサイト（v0.20.9。淳さんの指定で消す）
function Clear-LocalSites {
    param($Prof)
    $name = 'Local のサイト（Local Sites）'
    $p = Get-LocalSitesPath
    if (-not $p -or -not (Test-Path -LiteralPath $p)) { return New-Row 'localsites' $name 'SKIP' 'なし' '空' 'Local（WordPress）を使っていません。' }
    if (Test-IsProtected $p) { return New-Row 'localsites' $name 'FAIL' '' '空' 'このアプリの場所と重なるため、念のため消していません。' }
    $items = @(Get-ChildItem -LiteralPath $p -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'desktop.ini' })
    if ($items.Count -eq 0) { return New-Row 'localsites' $name 'SAME' '0 個' '空' '' }
    $stopped = Stop-LocalWpProcesses   # サイトを動かす部品（MySQL・nginx など）がファイルをつかんでいると消せないため
    $failed = Remove-Items $items
    $note = ''; if ($stopped -gt 0) { $note = ('サイトを動かしていた部品を {0} 個止めました。' -f $stopped) }
    return Format-CleanResult 'localsites' $name $items.Count $failed $note
}

# ---------------------------------------------------------------- アプリの設定（前の受講生が変えたもの。v0.20.10。淳さんの指定）
function Clear-AppSettings {
    param($Prof)
    $name = 'アプリの設定（前の受講生が変えたもの）'
    $items = Get-AppSettingItems
    if ($items.Count -eq 0) { return New-Row 'appsettings' $name 'SAME' 'なし' 'なし' '' }
    $done = 0; $failed = @(); $skipped = @()
    foreach ($it in $items) {
        $open = @(Get-Process -Name $it.Proc -ErrorAction SilentlyContinue)
        if ($open.Count -gt 0) { if ($skipped -notcontains $it.App) { $skipped += $it.App }; continue }   # 開いていると消せない・書き戻される
        try {
            if ($it.Kind -eq 'reg') { Remove-Item -LiteralPath $it.Path -Recurse -Force -ErrorAction Stop }
            else {
                if (Test-IsProtected $it.Path) { continue }
                $f = @(Get-Item -LiteralPath $it.Path -Force -ErrorAction Stop)
                if ((Remove-Items $f) -gt 0) { throw '消せませんでした' }
            }
            $done++
        } catch { if ($failed -notcontains $it.App) { $failed += $it.App } }
    }
    $apps = @($items | ForEach-Object { $_.App } | Select-Object -Unique) -join '・'
    $note = ''
    if ($skipped.Count) { $note += ('開いていたので消していないもの：' + ($skipped -join '・') + '（閉じてからもう一度［履歴を消す］）。') }
    if ($failed.Count) { $note += ('消せなかったもの：' + ($failed -join '・') + '。') }
    if ($failed.Count -or $skipped.Count) { return New-Row 'appsettings' $name 'FAIL' $apps 'なし' $note }
    return New-Row 'appsettings' $name 'DONE' $apps 'なし' '初めの状態に戻しました（次に開くと、アプリが作り直します）。'
}

# ---------------------------------------------------------------- まとめて（clean.ps1・oneclick.ps1 から使う）

function Invoke-CleanAll {
    # $Skip：飛ばす項目（関数の名前）。先生の片付けでは、移しきれなかったファイルを消さないよう、ダウンロード・デスクトップ・授業ファイルを飛ばす
    param($Prof, [System.Collections.Generic.List[object]]$Rows, [string[]]$Skip = @())
    $steps = @(
        @('Clear-RecentFiles',    '最近使ったファイル'),
        @('Clear-QuickAccessFrequent', 'エクスプローラーのよく使うフォルダー'),
        @('Clear-StartMenuRecent', 'スタートの「最近使用」'),
        @('Clear-ExplorerTyped',  'アドレス欄・「ファイル名を指定して実行」の入力履歴'),
        @('Clear-OfficeMru',      'Office の最近使ったファイル'),
        @('Clear-NotepadHistory', 'メモ帳の履歴'),
        @('Clear-ImeData',        '日本語入力（IME）の学習とユーザー辞書'),
        @('Clear-Downloads',      'ダウンロード フォルダー'),
        @('Clear-DesktopFiles',   'デスクトップのファイル'),
        @('Clear-UserRecycleBin', 'ごみ箱'),
        @('Clear-BrowserData',    'Edge・Chrome のお気に入り・履歴・Cookie'),
        @('Clear-WamAccounts',    'Windows に追加されたアカウント'),
        @('Clear-OfficeSignIn',   'Office のサインイン'),
        @('Clear-AdobeSignIn',    'Adobe Creative Cloud のサインイン'),
        @('Clear-StoredCredentials', '保存されたログイン情報'),
        @('Clear-LocalSites',     'Local のサイト'),
        @('Clear-AppSettings',    'アプリの設定'),
        @('Clear-SignInApps',     'ほかのアプリ・ブラウザーのログイン'),
        @('Clear-ClassFiles',     '受け取った授業ファイル')
    )
    foreach ($s in $steps) {
        $func = $s[0]; $label = $s[1]
        if ($Skip -contains $func) { continue }
        try {
            $rs = @(& $func $Prof)
        } catch {
            $rs = @(New-Row ($func.ToLower()) $label 'FAIL' '' '' ('消す途中で問題が起きました：' + $_.Exception.Message))
        }
        foreach ($r in $rs) {
            if ($null -eq $r -or -not $r.PSObject.Properties['State']) { continue }
            [void]$Rows.Add($r)
            Write-Host ('[{0}] {1} ／ {2} → {3} ／ {4}' -f $r.State, $r.Name, $r.Current, $r.Expected, $r.Detail)
        }
    }
    foreach ($d in $script:DetailLines) { Write-Host $d }
}

# まなびと講習準備 ― 点検（見るだけ。PC の設定は何も変えない）
#
# 使い方（画面のアプリから呼ばれる）:
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File inspect.ps1
#       -ProfilePath <プロファイル.json> -OutFile <結果.tsv> -DetailFile <詳細.txt> -Screen 1600x900x96 -AppPath <このアプリのフォルダー>
#
# 結果.tsv は UTF-8（BOM 付き）。1 行目は # で始まる見出し。
# 列: 区分 <TAB> ID <TAB> 項目 <TAB> 判定 <TAB> 今の状態 <TAB> 講習用の状態 <TAB> 説明

param(
    [string]$ProfilePath = '',
    [Parameter(Mandatory = $true)][string]$OutFile,
    [string]$DetailFile = '',
    [string]$Screen = '',
    [string]$AppPath = ''
)

# 外部コマンド（powercfg）のエラー出力で止まらないよう、全体は Continue にして
# 必要なところだけ -ErrorAction Stop を付ける
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

foreach ($m in @('common', 'power', 'appearance', 'sound', 'security', 'traces', 'apps', 'system')) {
    . (Join-Path $here ('modules\' + $m + '.ps1'))
}

$script:InspectAppPath = $AppPath

$prof = $null
$profError = ''
if ($ProfilePath) {
    try {
        $prof = Get-Content -LiteralPath $ProfilePath -Raw -Encoding UTF8 -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    } catch {
        $profError = $_.Exception.Message
    }
}

# 区分, 関数, 項目の名前（失敗したときに使う）
$checks = @(
    @('電源と画面',         'Test-Sleep',          '自動スリープ'),
    @('電源と画面',         'Test-DisplayOff',     '画面の自動消灯'),
    @('電源と画面',         'Test-Hibernate',      '自動の休止状態'),
    @('電源と画面',         'Test-LidAction',      'ふたを閉じたとき'),
    @('電源と画面',         'Test-PowerSource',    '電源と待機の方式'),
    @('電源と画面',         'Test-Screen',         '画面の解像度と拡大率'),
    @('見た目と通知',       'Test-Wallpaper',      'デスクトップの背景'),
    @('見た目と通知',       'Test-FileExtensions', 'ファイルの拡張子'),
    @('見た目と通知',       'Test-TaskbarWidgets', 'タスクバーのウィジェット'),
    @('見た目と通知',       'Test-TaskbarSearch',  'タスクバーの検索'),
    @('見た目と通知',       'Test-Notifications',  '通知（右下のお知らせ）'),
    @('見た目と通知',       'Test-OneDriveStartup','OneDrive の自動起動'),
    @('音',                 'Test-Volume',         '音量'),
    @('安全と更新',         'Test-Edition',        'Windows の種類'),
    @('安全と更新',         'Test-SmartAppControl','スマート アプリ コントロール'),
    @('安全と更新',         'Test-WindowsUpdate',  'Windows Update'),
    @('前の受講生の痕跡',   'Test-RecentFiles',    '最近使ったファイル'),
    @('前の受講生の痕跡',   'Test-ExplorerTyped',  'アドレス欄・「ファイル名を指定して実行」の入力履歴'),
    @('前の受講生の痕跡',   'Test-OfficeMru',      'Office の最近使ったファイル'),
    @('前の受講生の痕跡',   'Test-NotepadHistory', 'メモ帳の履歴（開いたままのタブ）'),
    @('前の受講生の痕跡',   'Test-ImeLearning',    '日本語入力（IME）の学習'),
    @('前の受講生の痕跡',   'Test-Downloads',      'ダウンロード フォルダー'),
    @('前の受講生の痕跡',   'Test-DesktopFiles',   'デスクトップのファイル'),
    @('前の受講生の痕跡',   'Test-RecycleBin',     'ごみ箱'),
    @('前の受講生の痕跡',   'Test-BrowserData',    'Edge・Chrome のお気に入り・履歴・Cookie'),
    @('前の受講生の痕跡',   'Test-ClassFiles',     '受け取った授業ファイル'),
    @('アプリ',             'Test-Office',         'Microsoft 365（Office）'),
    @('アプリ',             'Test-AdobeCC',        'Adobe Creative Cloud デスクトップアプリ'),
    @('アプリ',             'Test-OwnApps',        '独自アプリ'),
    @('この PC',            'Test-DefaultPrinter', '既定のプリンター')
)

function Clean-Field {
    param($Value)
    if ($null -eq $Value) { return '' }
    return ([string]$Value) -replace "[`t`r`n]+", ' '
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("#区分`tID`t項目`t判定`t今の状態`t講習用の状態`t説明")

$profName = '（なし）'
if ($prof) { $profName = [string](Get-Pref $prof 'name' (Split-Path -Leaf $ProfilePath)) }
Add-Detail ('まなびと講習準備 点検 {0:yyyy/MM/dd HH:mm:ss}' -f (Get-Date))
Add-Detail ('PC 名: {0}  利用者: {1}  プロファイル: {2}' -f $env:COMPUTERNAME, $env:USERNAME, $profName)
if ($profError) {
    Add-Detail ('プロファイルを読めませんでした: ' + $profError)
    $lines.Add(('プロファイル' + "`tprofile`t" + 'プロファイルの読み込み' + "`tERR`t`t`t" + (Clean-Field $profError)))
}

foreach ($c in $checks) {
    $group = $c[0]; $func = $c[1]; $label = $c[2]
    Add-Detail ''
    Add-Detail ('=== ' + $label + ' ===')
    $rows = @()
    try {
        if ($func -eq 'Test-Screen') {
            $rows = @(& $func $prof $Screen)
        } else {
            $rows = @(& $func $prof)
        }
    } catch {
        $rows = @(New-Row ($func.ToLower()) $label 'ERR' '' '' ('確認の途中で問題が起きました: ' + $_.Exception.Message))
    }
    foreach ($r in $rows) {
        if ($null -eq $r -or -not $r.PSObject.Properties['State']) { continue }
        $fields = @($group, $r.Id, $r.Name, $r.State, $r.Current, $r.Expected, $r.Detail) | ForEach-Object { Clean-Field $_ }
        $lines.Add(($fields -join "`t"))
        Add-Detail ('[{0}] {1} ／ 今: {2} ／ 講習用: {3}' -f $r.State, $r.Name, $r.Current, $r.Expected)
        if ($r.Detail) { Add-Detail ('    ' + $r.Detail) }
    }
}

$utf8 = New-Object System.Text.UTF8Encoding $true
if ($DetailFile) {
    [System.IO.File]::WriteAllLines($DetailFile, [string[]]$script:DetailLines.ToArray(), $utf8)
}
# 結果は最後にまとめて書く（途中で止まったときに、半端な結果が読まれないように）
$tmp = $OutFile + '.part'
[System.IO.File]::WriteAllLines($tmp, [string[]]$lines.ToArray(), $utf8)
Move-Item -LiteralPath $tmp -Destination $OutFile -Force
exit 0

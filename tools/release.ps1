# まなびと講習準備 ― 新しい版を GitHub に出す（淳さんの PC で 1 行で動かす）
#
# 使うアプリ：コマンドプロンプト
#   cd /d C:\dev\ManabitoClassSetup
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\release.ps1 -Zip "%USERPROFILE%\Downloads\ManabitoClassSetup_v0.19.6.zip"
#
# することと順番（途中で問題があれば、そこで止まる。印（タグ）は、main に送れたあとでだけ付ける）：
#   1. GitHub の変更を取り込む（git pull --rebase）
#   2. zip を展開し、中身をこのフォルダーに上書きでコピーする（.git と release\apps.json には触れない）
#   3. 目録（release\apps.json）を GitHub のものに戻す
#   4. 版の数字を src\main.cpp から読む。同じ版の印が GitHub にあれば止める
#   5. コミットして送る。断られたら、取り込み直してもう一度送る
#   6. 印 v<版> を付けて送る（GitHub の Actions がビルドして公開し、目録を書き換える）
# -Zip を省くと、2 を飛ばす（自分でコピーしたあとに使う）。-Message でコミットの説明を変えられる（省くと docs\CHANGES_v<版>.md の見出し）。

param(
    [string]$Zip = '',
    [string]$Message = ''
)

# git は進み具合などを「エラーの出口」に書くため、エラーで止まる設定にはしない。成功したかは終了コードで判断する
$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $repo

function Say { param([string]$Text, [string]$Color = 'Gray') Write-Host $Text -ForegroundColor $Color }
function Stop-Here { param([string]$Text) Say ''; Say ('止めました：' + $Text) 'Red'; Say '直してから、もう一度同じ命令を実行してください。' 'Red'; exit 1 }
# 名前を「Git」にしてはいけない：PowerShell では関数が git.exe より優先され、自分自身を呼び続ける（検証で判明）
function Invoke-GitCommand {
    # git を動かし、終了コードだけを返す（出力は画面に出すだけ。返り値に混ぜると判定を誤る）
    & git @args | Out-Host
    return $LASTEXITCODE
}

Say ('保管庫：' + $repo) 'Cyan'
if (-not (Test-Path -LiteralPath (Join-Path $repo '.git'))) { Stop-Here 'このフォルダーは Git の保管庫ではありません（.git がありません）。' }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Stop-Here 'git が見つかりません。' }

# 1. 取り込み（コピーの前に行う。手元に送っていない変更があると止まる）
Say ''
Say '1. GitHub の変更を取り込みます（git pull --rebase）' 'Cyan'
if ($Zip) {
    # zip を使うときは、前の作業の残り（コミットしていない変更）があると上書きで混ざるため止める
    $left = @(git status --porcelain | Where-Object { $_ })
    if ($left.Count -gt 0) { Stop-Here ('コミットしていない変更が残っています（' + $left.Count + ' 件）。前の作業を終えるか、git status で確かめてください。') }
}
if ((Invoke-GitCommand pull --rebase) -ne 0) { Stop-Here '取り込めませんでした（git pull --rebase）。' }

# 2. zip の中身を上書きでコピー
if ($Zip) {
    Say ''
    Say ('2. zip を展開して上書きでコピーします：' + $Zip) 'Cyan'
    if (-not (Test-Path -LiteralPath $Zip)) { Stop-Here ('zip が見つかりません：' + $Zip) }
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('manabito_release_' + [guid]::NewGuid().ToString('N'))
    try { Expand-Archive -LiteralPath $Zip -DestinationPath $tmp -Force -ErrorAction Stop } catch { Stop-Here ('zip を展開できませんでした：' + $_.Exception.Message) }
    $src = Join-Path $tmp 'ManabitoClassSetup'
    if (-not (Test-Path -LiteralPath (Join-Path $src 'src\main.cpp')) -and -not (Test-Path -LiteralPath (Join-Path $src 'src/main.cpp'))) {
        Remove-Item -LiteralPath $tmp -Recurse -Force
        Stop-Here 'zip の中に ManabitoClassSetup フォルダー（src\main.cpp）がありません。'
    }
    $srcFull = (Resolve-Path -LiteralPath $src).Path.TrimEnd('\', '/')
    $n = 0
    foreach ($f in @(Get-ChildItem -LiteralPath $srcFull -Recurse -File -Force)) {
        $rel = $f.FullName.Substring($srcFull.Length).TrimStart('\', '/')
        $relNorm = $rel -replace '/', '\'
        if ($relNorm -like '.git\*' -or $relNorm -ieq 'release\apps.json') { continue }   # 保管庫の記録と目録には触れない
        $dst = Join-Path $repo $rel
        $dir = Split-Path -Parent $dst
        if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
        Copy-Item -LiteralPath $f.FullName -Destination $dst -Force
        $n++
    }
    Remove-Item -LiteralPath $tmp -Recurse -Force
    Say ('   ' + $n + ' 個のファイルをコピーしました。')
} else {
    Say ''
    Say '2. zip の指定が無いので、コピーは飛ばします（手でコピーしたものを使います）。' 'Cyan'
}

# 3. 目録を戻す
Say ''
Say '3. 目録（release\apps.json）を GitHub のものに戻します' 'Cyan'
if ((Invoke-GitCommand checkout -- release/apps.json) -ne 0) { Stop-Here '目録を戻せませんでした。' }

# 4. 版の数字
$main = Get-Content -LiteralPath (Join-Path $repo 'src/main.cpp') -Raw -Encoding UTF8
if ($main -notmatch 'kVersion\[\]\s*=\s*L"(\d+\.\d+\.\d+)"') { Stop-Here 'src\main.cpp から版の数字を読めませんでした。' }
$ver = $Matches[1]
$tag = 'v' + $ver
Say ''
Say ('4. 版：' + $ver + '（印 ' + $tag + '）') 'Cyan'
$remoteTag = @(git ls-remote --tags origin ('refs/tags/' + $tag) | Where-Object { $_ })
if ($remoteTag.Count -gt 0) { Stop-Here ('GitHub に印 ' + $tag + ' がもうあります。版の数字を上げた zip を使ってください。') }
if (@(git tag --list $tag | Where-Object { $_ }).Count -gt 0) {
    # 手元だけにある古い印（前回の失敗の残り）は消してから付け直す
    Say ('   手元だけにある古い印 ' + $tag + ' を消します。') 'Yellow'
    if ((Invoke-GitCommand tag -d $tag) -ne 0) { Stop-Here ('手元の古い印 ' + $tag + ' を消せませんでした。') }
}

# 5. コミットして送る
if (-not $Message) {
    $changes = Join-Path $repo ('docs/CHANGES_' + $tag + '.md')
    $Message = $tag
    if (Test-Path -LiteralPath $changes) {
        $first = (Get-Content -LiteralPath $changes -Encoding UTF8 -TotalCount 1) -replace '^#\s*', ''
        if ($first) { $Message = $first }
    }
}
Say ''
Say ('5. コミットして送ります：' + $Message) 'Cyan'
if ((Invoke-GitCommand add -A) -ne 0) { Stop-Here 'git add に失敗しました。' }
$pending = @(git status --porcelain | Where-Object { $_ })
if ($pending.Count -gt 0) {
    if ((Invoke-GitCommand commit -m $Message) -ne 0) { Stop-Here 'コミットできませんでした。' }
} else {
    Say '   コミットする変更はありません（もうコミット済み）。'
}
if ((Invoke-GitCommand push) -ne 0) {
    Say '   送るのを断られたので、取り込み直してもう一度送ります。' 'Yellow'
    if ((Invoke-GitCommand pull --rebase) -ne 0) { Stop-Here '取り込み直せませんでした（git pull --rebase）。' }
    if ((Invoke-GitCommand push) -ne 0) { Stop-Here 'もう一度送っても断られました（git push）。' }
}

# 6. 印を付けて送る（main に送れたあとでだけ）
Say ''
Say ('6. 印 ' + $tag + ' を付けて送ります') 'Cyan'
if ((Invoke-GitCommand tag $tag) -ne 0) { Stop-Here ('印 ' + $tag + ' を付けられませんでした。') }
if ((Invoke-GitCommand push origin $tag) -ne 0) { Stop-Here ('印 ' + $tag + ' を送れませんでした。') }

Say ''
Say ('できました。GitHub の Actions がビルドして公開します（2 分ほど）。') 'Green'
Say ('   ビルドの様子：https://github.com/mucjun/manabito-class-setup/actions') 'Green'
Say ('   公開されたら、教室の PC で起動すると v' + $ver + ' への更新の案内が出ます（公開から 5 分ほど）。') 'Green'
Say '   次に作業するときも、この命令から始めれば、最初に GitHub の変更を取り込みます。' 'Green'
exit 0

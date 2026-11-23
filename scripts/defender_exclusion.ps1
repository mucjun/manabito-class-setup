# まなびと講習準備 ― Microsoft Defender（Windows セキュリティ）の除外に、このアプリの EXE を加える・外す
#
# ZIP のいちばん上の defender_exclude_add.cmd（加える）・defender_exclude_remove.cmd（外す）から呼ばれる。
# 除外するのは、Program Files に入れたこのアプリの EXE 1 つだけ（フォルダー全体・プロセス単位では除外しない）：
#   C:\Program Files\Org_Apps\ManabitoClassSetup\ManabitoClassSetup.exe
# この場所は管理者でないと書き換えられない。場所が決まっているので、アプリを更新しても除外はそのまま効く。
# 管理者の確認が出る。何をするかを表示し、「はい」のときだけ行う。

param([switch]$Remove)

$ErrorActionPreference = 'Continue'
function Wait-Close { Write-Host ''; Read-Host 'Enter キーを押すと閉じます' | Out-Null }

# 管理者でなければ、管理者として開き直す
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    $args2 = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $MyInvocation.MyCommand.Path + '"'))
    if ($Remove) { $args2 += '-Remove' }
    try { Start-Process -FilePath 'powershell.exe' -ArgumentList $args2 -Verb RunAs -ErrorAction Stop } catch { Write-Host '管理者の確認で「いいえ」が押されたため、何もしていません。'; Wait-Close }
    exit
}

$pf = $env:ProgramFiles; if (-not $pf) { $pf = 'C:\Program Files' }
$target = Join-Path $pf 'Org_Apps\ManabitoClassSetup\ManabitoClassSetup.exe'

Write-Host 'まなびと講習準備 ― Windows セキュリティの除外' -ForegroundColor Cyan
Write-Host ''
if (-not (Get-Command Add-MpPreference -ErrorAction SilentlyContinue)) {
    Write-Host 'この PC では Microsoft Defender の設定を変えられません（ほかのウイルス対策ソフトを使っている場合など）。' -ForegroundColor Yellow
    Wait-Close; exit 1
}

function Test-Excluded {
    try { return (@((Get-MpPreference -ErrorAction Stop).ExclusionPath) -contains $target) } catch { return $false }
}

if ($Remove) {
    Write-Host ('除外から外します：' + $target)
    if (-not (Test-Excluded)) { Write-Host '除外には入っていません。何もしていません。' -ForegroundColor Green; Wait-Close; exit 0 }
    $ans = Read-Host '外しますか？（はい＝Y／いいえ＝N）'
    if ($ans -notmatch '^[yYはｙＹ]') { Write-Host '何もしていません。'; Wait-Close; exit 0 }
    try { Remove-MpPreference -ExclusionPath $target -ErrorAction Stop } catch { Write-Host ('外せませんでした：' + $_.Exception.Message) -ForegroundColor Red; Wait-Close; exit 1 }
    if (Test-Excluded) { Write-Host '外せませんでした（組織の管理で決められている可能性があります）。' -ForegroundColor Red; Wait-Close; exit 1 }
    Write-Host '除外から外しました。' -ForegroundColor Green
    Wait-Close; exit 0
}

Write-Host '次のファイル 1 つを、Windows セキュリティ（Microsoft Defender）が調べない「除外」に加えます。'
Write-Host ('   ' + $target)
Write-Host ''
Write-Host '・このアプリが「トロイの木馬」と誤って判定され、消されるのを防ぐためのものです。'
Write-Host '・除外するのはこのファイルだけです。ほかのファイルやフォルダーは、今までどおり調べます。'
Write-Host '・この場所は管理者でないと書き換えられません。アプリを更新しても、除外はそのまま効きます。'
Write-Host '・元に戻すときは、defender_exclude_remove.cmd を実行します。'
Write-Host ''
if (-not (Test-Path -LiteralPath $target)) {
    Write-Host 'このアプリは、まだ Program Files に入っていません。先に まなびと講習準備 を起動し、「Program Files に入れて起動する」を選んでから、もう一度実行してください。' -ForegroundColor Yellow
    Wait-Close; exit 1
}
if (Test-Excluded) { Write-Host 'もう除外に入っています。何もしていません。' -ForegroundColor Green; Wait-Close; exit 0 }
$ans = Read-Host '加えますか？（はい＝Y／いいえ＝N）'
if ($ans -notmatch '^[yYはｙＹ]') { Write-Host '何もしていません。'; Wait-Close; exit 0 }
try { Add-MpPreference -ExclusionPath $target -ErrorAction Stop } catch { Write-Host ('加えられませんでした：' + $_.Exception.Message) -ForegroundColor Red; Wait-Close; exit 1 }
if (-not (Test-Excluded)) { Write-Host '加えられませんでした（組織の管理で決められている可能性があります）。Windows セキュリティの画面から手で加えてください（README の手順）。' -ForegroundColor Red; Wait-Close; exit 1 }
Write-Host '除外に加えました。' -ForegroundColor Green
Wait-Close; exit 0

# まなびと講習準備 ― Program Files に入れる（自分自身のインストール）
#
# 画面のアプリが、Program Files 以外から起動されたときに「管理者として」呼ぶ:
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File install.ps1
#       -Source <起動したフォルダー> -Target <C:\Program Files\Org_Apps\ManabitoClassSetup> -OutFile <結果.txt>
#
# ・EXE と scripts は入れ替える（古い版の部品が残らないよう、scripts は一度消してから写す）。
# ・profiles は上書きする。ただし、入っていたほうの中身が違うときは profiles_backup に控えを残す。
#   入っていたほうにしか無いプロファイル（その PC で足したもの）は残す。
# ・デスクトップ（全員）とスタートメニュー（全員）にショートカットを作る。
# 結果.txt の 1 行目は OK か NG、2 行目から説明。

param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Target,
    [Parameter(Mandatory = $true)][string]$OutFile
)

$ErrorActionPreference = 'Stop'
$log = New-Object System.Collections.Generic.List[string]

function Write-Out {
    param([bool]$Ok)
    $head = 'NG'; if ($Ok) { $head = 'OK' }
    $all = @($head) + $log.ToArray()
    [System.IO.File]::WriteAllLines($OutFile, [string[]]$all, (New-Object System.Text.UTF8Encoding $true))
}

try {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw '管理者として動いていません。'
    }
    $src = [System.IO.Path]::GetFullPath($Source).TrimEnd('\')
    $dst = [System.IO.Path]::GetFullPath($Target).TrimEnd('\')
    if ($src -ieq $dst) { throw '入れる元と先が同じです。' }
    if (-not (Test-Path -LiteralPath (Join-Path $src 'ManabitoClassSetup.exe'))) { throw ('元のフォルダーに ManabitoClassSetup.exe がありません：' + $src) }
    if (-not (Test-Path -LiteralPath (Join-Path $src 'scripts'))) { throw ('元のフォルダーに scripts がありません：' + $src) }

    New-Item -ItemType Directory -Path $dst -Force | Out-Null

    # profiles：中身が違うものは控えを残してから上書き
    $srcProf = Join-Path $src 'profiles'
    $dstProf = Join-Path $dst 'profiles'
    if ((Test-Path -LiteralPath $srcProf) -and (Test-Path -LiteralPath $dstProf)) {
        $stamp = (Get-Date).ToString('yyyyMMdd_HHmmss')
        foreach ($f in Get-ChildItem -LiteralPath $dstProf -File) {
            $s = Join-Path $srcProf $f.Name
            if (-not (Test-Path -LiteralPath $s)) { continue }
            if ((Get-FileHash -LiteralPath $s).Hash -ne (Get-FileHash -LiteralPath $f.FullName).Hash) {
                $bk = Join-Path $dst 'profiles_backup'
                New-Item -ItemType Directory -Path $bk -Force | Out-Null
                Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $bk ($f.BaseName + '_' + $stamp + $f.Extension)) -Force
                $log.Add('プロファイルの控え: ' + $f.Name + ' → profiles_backup')
            }
        }
    }

    # scripts は入れ替え（古い部品を残さない）
    $dstScripts = Join-Path $dst 'scripts'
    if (Test-Path -LiteralPath $dstScripts) { Remove-Item -LiteralPath $dstScripts -Recurse -Force }
    Copy-Item -LiteralPath (Join-Path $src 'scripts') -Destination $dstScripts -Recurse -Force
    if (Test-Path -LiteralPath $srcProf) {
        New-Item -ItemType Directory -Path $dstProf -Force | Out-Null
        Copy-Item -Path (Join-Path $srcProf '*') -Destination $dstProf -Recurse -Force
    }
    Copy-Item -LiteralPath (Join-Path $src 'ManabitoClassSetup.exe') -Destination (Join-Path $dst 'ManabitoClassSetup.exe') -Force
    $log.Add('入れた先: ' + $dst)

    # ショートカット（全員のデスクトップとスタートメニュー）
    $exe = Join-Path $dst 'ManabitoClassSetup.exe'
    $ws = New-Object -ComObject WScript.Shell
    $tools = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'まなびとツール'
    if (-not (Test-Path -LiteralPath $tools)) { New-Item -ItemType Directory -Path $tools -Force | Out-Null }
    $oldStart = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'まなびと講習準備.lnk'   # v0.15 までの場所
    if (Test-Path -LiteralPath $oldStart) { Remove-Item -LiteralPath $oldStart -Force -ErrorAction SilentlyContinue }
    foreach ($dir in @([Environment]::GetFolderPath('CommonDesktopDirectory'), $tools)) {
        if (-not $dir) { continue }
        $lnk = $ws.CreateShortcut((Join-Path $dir 'まなびと講習準備.lnk'))
        $lnk.TargetPath = $exe
        $lnk.WorkingDirectory = $dst
        $lnk.Description = 'まなびと講習準備'
        $lnk.Save()
    }
    $log.Add('ショートカット: デスクトップ（全員）・スタートメニューの「まなびとツール」')

    # ファイアウォール：授業ファイルの配布サーバーの受け口（このアプリだけ、プライベートとドメインのネットワークだけ）
    $rule = 'まなびと講習準備（授業ファイルの配布）'
    & netsh.exe advfirewall firewall delete rule name="$rule" | Out-Null
    & netsh.exe advfirewall firewall add rule name="$rule" dir=in action=allow program="$exe" enable=yes profile=private,domain | Out-Null
    if ($LASTEXITCODE -eq 0) { $log.Add('ファイアウォール: 配布サーバーの受け口を登録（プライベート・ドメイン）') }
    else { $log.Add('ファイアウォール: 受け口を登録できませんでした（配布サーバーになるとき、Windows が許可を求めることがあります）') }
    Write-Out $true
    exit 0
} catch {
    $log.Add($_.Exception.Message)
    Write-Out $false
    exit 1
}

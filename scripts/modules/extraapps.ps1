# まなびと講習準備 ― 講座のプロファイルで決める追加のアプリ（v0.19.0）
#
# プロファイルのキー：
#   extraApps   … 入れるアプリの Id をカンマ区切り（例 "acrobat-reader,paintdotnet"）。Id は scripts\extra_apps.txt の 1 列目
#   pdfDefault  … PDF を開くアプリの Id（空なら変えない）
# 入れ方：winget（画面を出さない・同意は自動・PC 全体に入れる）。入ったかは、アンインストール情報の DisplayName で確かめる。
# PDF の関連付け：Windows が直接の書き換えを止めているため（UCPD）、公式のポリシー「既定の関連付けの構成ファイル」を使う。次のサインインから効く。

function Get-ExtraCatalog {
    $file = Join-Path (Split-Path -Parent $PSScriptRoot) 'extra_apps.txt'
    $list = @()
    if (-not (Test-Path -LiteralPath $file)) { return $list }
    foreach ($line in @(Get-Content -LiteralPath $file -Encoding UTF8)) {
        $l = ([string]$line).TrimStart([char]0xFEFF).Trim()
        if (-not $l -or $l.StartsWith('#')) { continue }
        $f = $l.Split('|')
        if ($f.Count -lt 6) { continue }
        $list += [pscustomobject]@{ Id = $f[0]; Name = $f[1]; Winget = $f[2]; Detect = $f[3]; Pdf = ($f[4] -eq '1'); Note = $f[5] }
    }
    return $list
}

function Get-ProfileExtraIds {
    param($Prof)
    return @(([string](Get-Pref $Prof 'extraApps' '')).Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

# アンインストール情報（PC 全体の 64・32 ビット、利用者）から探す。戻り値：@{ Name; Version } か $null
function Find-InstalledApp {
    param([string]$Pattern)
    foreach ($k in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
                     'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall')) {
        if (-not (Test-Path -LiteralPath $k)) { continue }
        foreach ($sub in @(Get-ChildItem -LiteralPath $k -ErrorAction SilentlyContinue)) {
            $dn = [string]$sub.GetValue('DisplayName', '')
            if ($dn -and $dn -like $Pattern) { return @{ Name = $dn; Version = [string]$sub.GetValue('DisplayVersion', '') } }
        }
    }
    return $null
}

function Get-WingetPath {
    $c = Get-Command winget.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($c) { return $c.Source }
    if ($env:LOCALAPPDATA) {
        $p = Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps\winget.exe'
        if (Test-Path -LiteralPath $p) { return $p }
    }
    return $null
}

function Test-ExtraApps {
    param($Prof)
    $ids = Get-ProfileExtraIds $Prof
    $cat = Get-ExtraCatalog
    $rows = @()
    foreach ($id in $ids) {
        $app = $cat | Where-Object { $_.Id -eq $id } | Select-Object -First 1
        if (-not $app) { $rows += New-Row ('extra-' + $id) $id 'NA' '目録にありません' '入っている' 'scripts\extra_apps.txt に、この Id がありません。'; continue }
        $hit = Find-InstalledApp $app.Detect
        if ($hit) { $rows += New-Row ('extra-' + $id) $app.Name 'OK' ('入っている（' + $hit.Version + '）') '入っている' $app.Note }
        else { $rows += New-Row ('extra-' + $id) $app.Name 'NG' '入っていない' '入っている' ($app.Note + '。［まとめて実行］か［今すぐ実行］で入ります。') }
    }
    # PDF を開くアプリは点検しない（サインインし直すまで切り替わらず、点検では分かりにくいため。淳さんの指定）。設定は［設定をそろえる］で行う
    return $rows
}

# ---- Adobe Acrobat Reader：初回の確認と有料版の案内を出さない（Adobe の管理者向けの設定）
function Set-AcrobatQuiet {
    $failed = 0
    foreach ($prod in @('Acrobat Reader', 'Adobe Acrobat')) {   # 64 ビット版は「Adobe Acrobat」の名前で入ることがある
        $base = 'HKLM:\SOFTWARE\Policies\Adobe\' + $prod + '\DC\FeatureLockDown'
        foreach ($v in @(@($base, 'bToggleFTE', 1), @($base, 'bAcroSuppressUpsell', 1), @(($base + '\cIPM'), 'bShowMsgAtLaunch', 0))) {
            try { Set-RegDword $v[0] $v[1] ([int]$v[2]) } catch { $failed++ }
        }
        try { Set-RegDword ('HKCU:\Software\Adobe\' + $prod + '\DC\AdobeViewer') 'EULA' 1 } catch { $failed++ }   # 使用許諾に同意済み（この利用者）
    }
    return $failed
}

# ---- PDF を開くアプリ（公式のポリシー：既定の関連付けの構成ファイル）
function Get-PdfAssocXmlPath {
    $pd = $env:ProgramData; if (-not $pd) { $pd = 'C:\ProgramData' }
    return (Join-Path $pd 'ManabitoClassSetup\default_associations.xml')
}

# Acrobat の ProgId（64 ビット版は Acrobat.Document.DC、32 ビット版は AcroExch.Document.DC）
function Get-AcrobatProgId {
    foreach ($id in @('Acrobat.Document.DC', 'AcroExch.Document.DC')) {
        if (Test-Path -LiteralPath ('HKLM:\SOFTWARE\Classes\' + $id)) { return $id }
    }
    return $null
}

function Test-PdfDefault {
    param($Prof)
    $name = 'PDF を開くアプリ'
    $want = [string](Get-Pref $Prof 'pdfDefault' '')
    $userChoice = Get-RegValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\UserChoice' 'ProgId'
    $policy = Get-RegValue 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\System' 'DefaultAssociationsConfiguration'
    $cur = '既定（' + $(if ($userChoice) { $userChoice } else { 'Windows の既定' }) + '）'
    if ($want -ne 'acrobat-reader') { return New-Row 'pdfdefault' $name 'NA' $cur '変えない' '' }
    $prog = Get-AcrobatProgId
    if (-not $prog) { return New-Row 'pdfdefault' $name 'NG' $cur 'Adobe Acrobat Reader' 'Adobe Acrobat Reader が入っていません。' }
    if ($userChoice -eq $prog) { return New-Row 'pdfdefault' $name 'OK' 'Adobe Acrobat Reader' 'Adobe Acrobat Reader' '' }
    $note = '［設定をそろえる］で、Windows の公式のポリシー（既定の関連付けの構成ファイル）を設定します。次にサインインしたときから効きます。'
    if ($policy -and ($policy -eq (Get-PdfAssocXmlPath))) { $note = 'ポリシーは設定済みです。次にサインインしたとき（再起動のあと）に切り替わります。' }
    return New-Row 'pdfdefault' $name 'NG' $cur 'Adobe Acrobat Reader' $note
}

function Set-PdfDefault {
    param($Prof)
    $name = 'PDF を開くアプリ'
    $want = [string](Get-Pref $Prof 'pdfDefault' '')
    $key = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\System'
    $xml = Get-PdfAssocXmlPath
    if ($want -ne 'acrobat-reader') {
        # このアプリが設定したポリシーだけを外す（ほかで設定したものには触れない）
        if ((Get-RegValue $key 'DefaultAssociationsConfiguration') -eq $xml) {
            try { Remove-ItemProperty -LiteralPath $key -Name 'DefaultAssociationsConfiguration' -Force -ErrorAction Stop } catch { }
        }
        return New-Row 'pdfdefault' $name 'SKIP' '' '変えない' ''
    }
    $prog = Get-AcrobatProgId
    if (-not $prog) { return New-Row 'pdfdefault' $name 'SKIP' '' 'Adobe Acrobat Reader' 'Adobe Acrobat Reader が入っていないため、設定しませんでした（入れたあとに［設定をそろえる］で設定されます）。' }
    $content = "<?xml version=""1.0"" encoding=""UTF-8""?>`r`n<DefaultAssociations>`r`n  <Association Identifier="".pdf"" ProgId=""$prog"" ApplicationName=""Adobe Acrobat Reader"" />`r`n</DefaultAssociations>`r`n"
    try {
        New-Item -ItemType Directory -Path (Split-Path -Parent $xml) -Force | Out-Null
        [System.IO.File]::WriteAllText($xml, $content, (New-Object System.Text.UTF8Encoding $false))
        if (-not (Test-Path -LiteralPath $key)) { New-Item -Path $key -Force | Out-Null }
        New-ItemProperty -LiteralPath $key -Name 'DefaultAssociationsConfiguration' -Value $xml -PropertyType String -Force | Out-Null
    } catch {
        return New-Row 'pdfdefault' $name 'FAIL' '' 'Adobe Acrobat Reader' ('ポリシーを設定できませんでした：' + $_.Exception.Message)
    }
    return New-Row 'pdfdefault' $name 'DONE' '' 'Adobe Acrobat Reader' '公式のポリシー（既定の関連付けの構成ファイル）を設定しました。次にサインインしたとき（再起動のあと）から、PDF は Adobe Acrobat Reader で開きます。'
}

# ［設定をそろえる］：入っている追加アプリの設定（Acrobat の初回の確認など）と、PDF を開くアプリ
function Set-ExtraAppSettings {
    param($Prof)
    $ids = Get-ProfileExtraIds $Prof
    $rows = @()
    if ($ids -contains 'acrobat-reader' -and (Find-InstalledApp 'Adobe Acrobat*')) {
        $f = Set-AcrobatQuiet
        $rows += New-Row 'acrobat-quiet' 'Adobe Acrobat Reader の初回の確認' $(if ($f) { 'SKIP' } else { 'DONE' }) '' '出さない' $(if ($f) { ('{0} か所は書けませんでした。' -f $f) } else { '最初の画面・有料版の案内・起動時のお知らせを出さず、使用許諾に同意済みにしました。' })
    }
    $rows += Set-PdfDefault $Prof
    return $rows
}

# ［今すぐ実行］［まとめて実行］：プロファイルの追加アプリを winget で入れる
function Install-ExtraApps {
    param($Prof, [string]$ProfileDir)
    $ids = Get-ProfileExtraIds $Prof
    $cat = Get-ExtraCatalog
    $rows = @()
    $winget = Get-WingetPath
    $i = 0
    foreach ($id in $ids) {
        $i++
        $app = $cat | Where-Object { $_.Id -eq $id } | Select-Object -First 1
        if (-not $app) { $rows += New-Row ('extra-' + $id) $id 'FAIL' '' '入っている' '目録（scripts\extra_apps.txt）に、この Id がありません。'; continue }
        Set-DeployStep $i ($app.Name) $ids.Count
        $hit = Find-InstalledApp $app.Detect
        $installedNow = $false
        if (-not $hit) {
            if (-not $winget) {
                $rows += New-Row ('extra-' + $id) $app.Name 'FAIL' '入っていない' '入っている' 'winget がありません。Microsoft Store で「アプリ インストーラー」を入れるか、更新してください。'
                continue
            }
            # $args は PowerShell の特別な名前なので使わない
            $wingetArgs = @('install', '--id', $app.Winget, '--exact', '--source', 'winget', '--silent', '--scope', 'machine',
                            '--accept-package-agreements', '--accept-source-agreements', '--disable-interactivity')
            $code = Start-ProcessWithProgress $winget $wingetArgs $script:WorkDir ($app.Name + ' を入れています') 0
            $hit = Find-InstalledApp $app.Detect
            if (-not $hit) {
                # アプリによっては「PC 全体に入れる」用意が無い：指定を外して、もう一度だけ試す
                $retryArgs = @($wingetArgs | Where-Object { $_ -ne '--scope' -and $_ -ne 'machine' })
                Write-Host ($app.Name + '：PC 全体への指定を外して、もう一度試します（終了コード ' + $code + '）。')
                $code = Start-ProcessWithProgress $winget $retryArgs $script:WorkDir ($app.Name + ' を入れています（2 回目）') 0
                $hit = Find-InstalledApp $app.Detect
            }
            if (-not $hit) {
                $rows += New-Row ('extra-' + $id) $app.Name 'FAIL' '入っていない' '入っている' ('winget の終了コード ' + $code + '。' + $script:LastProcessSummary)
                continue
            }
            $note = 'winget で入れました（' + $hit.Version + '）。'
            $installedNow = $true
        } else {
            $note = ''
        }
        # 入れたあとの設定
        if ($id -eq 'acrobat-reader') {
            $f = Set-AcrobatQuiet
            $note += $(if ($f) { '初回の確認を止める設定のうち ' + $f + ' か所は書けませんでした。' } else { '初回の確認・有料版の案内を出さない設定を入れました。' })
        }
        if ($id -eq 'vscode') { $note += (Install-VSCodeJapanese) }
        $state = 'SAME'; if ($installedNow) { $state = 'DONE' }
        $rows += New-Row ('extra-' + $id) $app.Name $state $(if ($state -eq 'DONE') { '入っていない' } else { '入っている（' + $hit.Version + '）' }) '入っている' $note
    }
    return $rows
}

# VS Code の日本語化（拡張機能）。この利用者に入れる
function Install-VSCodeJapanese {
    $code = $null
    $places = @()
    if ($env:ProgramFiles) { $places += (Join-Path $env:ProgramFiles 'Microsoft VS Code\bin\code.cmd') }
    if ($env:LOCALAPPDATA) { $places += (Join-Path $env:LOCALAPPDATA 'Programs\Microsoft VS Code\bin\code.cmd') }
    foreach ($p in $places) {
        if (Test-Path -LiteralPath $p) { $code = $p; break }
    }
    if (-not $code) { return '日本語化の拡張機能は、VS Code の場所が分からず入れられませんでした。' }
    try {
        $out = & $code --install-extension 'MS-CEINTL.vscode-language-pack-ja' --force 2>&1
        if ($LASTEXITCODE -eq 0) { return '日本語化の拡張機能を入れました。' }
        return ('日本語化の拡張機能を入れられませんでした（' + (($out | Select-Object -Last 1) -join '') + '）。')
    } catch { return ('日本語化の拡張機能を入れられませんでした（' + $_.Exception.Message + '）。') }
}

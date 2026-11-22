# まなびと講習準備 ― 前の受講生のサインインを調べる・消す
#
# 対象（受講生が自分のアカウントで入った可能性があるもの）:
#   ・Windows に追加されたアカウント（Office・Store・Copilot などがサインインすると「メールとアカウント」に残る）
#   ・Office のサインイン（とライセンス）
#   ・Adobe Creative Cloud のサインイン
#   ・資格情報マネージャーの「汎用資格情報」（アプリが保存したログイン）
#   ・受講生が入れた可能性のあるアプリ（Zoom・LINE・Discord・ChatGPT・Claude など）のデータ（ログインごと消える）
#   ・Edge・Chrome 以外のブラウザー（Firefox・Brave・Opera・Vivaldi）
# PC 自体の Windows のサインイン（ローカル アカウント）と、ネットワーク共有・プリンターの資格情報（Windows 資格情報）には触れない。

# 開いているアプリの名前（同じ利用者のものだけ）
function Get-OpenAppNames {
    param([string[]]$Names)
    $sid = (Get-Process -Id $PID).SessionId
    return @($Names | Where-Object { @(Get-Process -Name $_ -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sid }).Count -gt 0 })
}

# ---------------------------------------------------------------- Windows に追加されたアカウント（WAM）

$script:WamReady = $false
function Initialize-Wam {
    if ($script:WamReady) { return $true }
    try {
        Add-Type -AssemblyName System.Runtime.WindowsRuntime -ErrorAction Stop
        $null = [Windows.Security.Authentication.Web.Core.WebAuthenticationCoreManager, Windows.Security.Authentication.Web.Core, ContentType = WindowsRuntime]
        $null = [Windows.Security.Credentials.WebAccountProvider, Windows.Security.Credentials, ContentType = WindowsRuntime]
        $null = [Windows.Security.Authentication.Web.Core.FindAllAccountsResult, Windows.Security.Authentication.Web.Core, ContentType = WindowsRuntime]
        $m = [System.WindowsRuntimeSystemExtensions].GetMethods()
        $script:AsTaskOp = $m | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
        $script:AsTaskAction = $m | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction' } | Select-Object -First 1
        $script:WamReady = ($null -ne $script:AsTaskOp -and $null -ne $script:AsTaskAction)
    } catch { $script:WamReady = $false }
    return $script:WamReady
}

function Wait-WinRtOp {
    param($Op, [Type]$ResultType)
    $t = $script:AsTaskOp.MakeGenericMethod($ResultType).Invoke($null, @($Op))
    [void]$t.Wait(30000)
    return $t.Result
}

# Office が使うアプリの番号（マイクロソフトの「WAM アカウントのサインアウト」の手順と同じ）
$script:OfficeClientId = 'd3590ed6-52b3-4102-aeff-aad2292ab01c'

# 戻り値：アカウントの一覧（WebAccount）。調べられなければ $null
function Get-WamAccounts {
    if (-not (Initialize-Wam)) { return $null }
    $all = New-Object System.Collections.Generic.List[object]
    foreach ($authority in @('organizations', 'consumers')) {
        try {
            $provider = Wait-WinRtOp ([Windows.Security.Authentication.Web.Core.WebAuthenticationCoreManager]::FindAccountProviderAsync('https://login.microsoft.com', $authority)) ([Windows.Security.Credentials.WebAccountProvider])
            if (-not $provider) { continue }
            $res = Wait-WinRtOp ([Windows.Security.Authentication.Web.Core.WebAuthenticationCoreManager]::FindAllAccountsAsync($provider, $script:OfficeClientId)) ([Windows.Security.Authentication.Web.Core.FindAllAccountsResult])
            if ($res -and $res.Accounts) { foreach ($a in $res.Accounts) { $all.Add($a) } }
        } catch { Write-Host ('（Windows のアカウント［' + $authority + '］を調べられませんでした：' + $_.Exception.Message + '）') }
    }
    return ,$all
}

function Test-WamAccounts {
    param($Prof)
    $name = 'Windows に追加されたアカウント（Microsoft・職場や学校）'
    $acc = Get-WamAccounts
    if ($null -eq $acc) { return New-Row 'signin-wam' $name 'NA' '調べられません' 'なし' 'Windows PowerShell 5.1 でだけ調べられます。' }
    $state = 'OK'; if ($acc.Count -gt 0) { $state = 'NG' }
    return New-Row 'signin-wam' $name $state ('{0} 件' -f $acc.Count) 'なし' 'Office などにサインインすると、Windows の「メールとアカウント」に残ります。'
}

function Clear-WamAccounts {
    param($Prof)
    $name = 'Windows に追加されたアカウント（Microsoft・職場や学校）'
    $acc = Get-WamAccounts
    if ($null -eq $acc) { return New-Row 'signin-wam' $name 'SKIP' '調べられません' 'なし' '' }
    if ($acc.Count -eq 0) { return New-Row 'signin-wam' $name 'SAME' '0 件' 'なし' '' }
    $failed = 0
    foreach ($a in $acc) {
        try {
            $t = $script:AsTaskAction.Invoke($null, @($a.SignOutAsync()))
            [void]$t.Wait(30000)
        } catch { $failed++ }
    }
    $state = 'DONE'; if ($failed -gt 0) { $state = 'FAIL' }
    return New-Row 'signin-wam' $name $state ('{0} 件' -f $acc.Count) 'なし' $(if ($failed) { ('{0} 件はサインアウトできませんでした。設定 →「アカウント」→「メールとアカウント」から削除してください。' -f $failed) } else { 'サインアウトしました。' })
}

# ---------------------------------------------------------------- Office のサインインとライセンス

function Get-OfficeIdentityKeys {
    $base = 'HKCU:\Software\Microsoft\Office\16.0\Common\Identity\Identities'
    if (-not (Test-Path -LiteralPath $base)) { return @() }
    return @(Get-ChildItem -LiteralPath $base -ErrorAction SilentlyContinue)
}

function Get-OfficeSignInPaths {
    param([bool]$WithLicenses)
    if (-not $env:LOCALAPPDATA) { return @() }
    $p = @(
        (Join-Path $env:LOCALAPPDATA 'Microsoft\IdentityCache'),
        (Join-Path $env:LOCALAPPDATA 'Microsoft\OneAuth')
    )
    if ($WithLicenses) {
        $p += (Join-Path $env:LOCALAPPDATA 'Microsoft\Office\Licenses')
        $p += (Join-Path $env:LOCALAPPDATA 'Microsoft\Office\16.0\Licensing')
    }
    return @($p | Where-Object { Test-Path -LiteralPath $_ })
}

function Test-OfficeSignIn {
    param($Prof)
    $name = 'Office のサインイン'
    $ids = Get-OfficeIdentityKeys
    $paths = Get-OfficeSignInPaths ([bool](Get-Pref $Prof 'officeRemoveLicenses' $true))
    $state = 'OK'; if ($ids.Count -gt 0) { $state = 'NG' }
    return New-Row 'signin-office' $name $state ('アカウント {0} 件' -f $ids.Count) 'なし' ('サインインの記録のフォルダー ' + $paths.Count + ' 個')
}

function Clear-OfficeSignIn {
    param($Prof)
    $name = 'Office のサインイン'
    $open = Get-OpenAppNames @('WINWORD', 'EXCEL', 'POWERPNT', 'OUTLOOK', 'ONENOTE', 'MSACCESS', 'MSPUB', 'lync', 'ms-teams')
    $withLic = [bool](Get-Pref $Prof 'officeRemoveLicenses' $true)
    $ids = Get-OfficeIdentityKeys
    $paths = Get-OfficeSignInPaths $withLic
    if ($ids.Count -eq 0 -and $paths.Count -eq 0) { return New-Row 'signin-office' $name 'SAME' 'なし' 'なし' '' }
    if ($open.Count -gt 0) {
        return New-Row 'signin-office' $name 'FAIL' ('アカウント {0} 件' -f $ids.Count) 'なし' ('Office が開いているため消せませんでした（' + ($open -join '、') + '）。閉じてからもう一度実行してください。')
    }
    $failed = 0
    foreach ($k in @('HKCU:\Software\Microsoft\Office\16.0\Common\Identity\Identities', 'HKCU:\Software\Microsoft\Office\16.0\Common\Identity\Profiles')) {
        if (Test-Path -LiteralPath $k) { try { Remove-Item -LiteralPath $k -Recurse -Force -ErrorAction Stop } catch { $failed++ } }
    }
    foreach ($p in $paths) {
        try { Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction Stop } catch { $failed++ }
    }
    $note = 'サインインの記録を消しました。'
    if ($withLic) { $note += '受講生のアカウントで有効にしたライセンスも消しました（次の受講生がサインインすると有効になります）。' }
    $state = 'DONE'; if ($failed -gt 0) { $state = 'FAIL'; $note = ('{0} か所は消せませんでした（使用中の可能性）。' -f $failed) }
    return New-Row 'signin-office' $name $state ('アカウント {0} 件' -f $ids.Count) 'なし' $note
}

# ---------------------------------------------------------------- Adobe Creative Cloud のサインイン

$script:AdobeProcesses = @('Creative Cloud', 'Adobe Desktop Service', 'AdobeIPCBroker', 'CCXProcess', 'CCLibrary', 'CoreSync', 'AdobeNotificationClient', 'Adobe CEF Helper', 'AdobeCollabSync')

function Get-AdobeSignInFiles {
    if (-not $env:LOCALAPPDATA) { return @() }
    $dir = Join-Path $env:LOCALAPPDATA 'Adobe\OOBE'
    if (-not (Test-Path -LiteralPath $dir)) { return @() }
    return @(Get-ChildItem -LiteralPath $dir -Force -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -like 'opm.db*' })
}

function Test-AdobeSignIn {
    param($Prof)
    $f = Get-AdobeSignInFiles
    $state = 'OK'; if ($f.Count -gt 0) { $state = 'NG' }
    $cur = 'なし'; if ($f.Count -gt 0) { $cur = 'サインインの記録あり' }
    return New-Row 'signin-adobe' 'Adobe Creative Cloud のサインイン' $state $cur 'なし' ''
}

function Clear-AdobeSignIn {
    param($Prof)
    $name = 'Adobe Creative Cloud のサインイン'
    $f = Get-AdobeSignInFiles
    if ($f.Count -eq 0) { return New-Row 'signin-adobe' $name 'SAME' 'なし' 'なし' '' }
    $open = Get-OpenAppNames @('Photoshop', 'Illustrator', 'InDesign', 'AfterFX', 'Adobe Premiere Pro', 'Acrobat', 'Animate', 'Dreamweaver', 'Lightroom', 'Adobe Audition')
    if ($open.Count -gt 0) {
        return New-Row 'signin-adobe' $name 'FAIL' 'サインインの記録あり' 'なし' ('Adobe のアプリが開いているため消せませんでした（' + ($open -join '、') + '）。閉じてからもう一度実行してください。')
    }
    foreach ($n in $script:AdobeProcesses) { Stop-Browser $n }  # Creative Cloud を閉じる（同じ利用者のものだけ）
    $failed = Remove-Items $f
    $state = 'DONE'; $note = 'サインインの記録を消しました。次に Creative Cloud を開くとサインインを求められます。'
    if ($failed -gt 0) { $state = 'FAIL'; $note = 'Creative Cloud が使用中で消せませんでした。Creative Cloud を終了してから、もう一度実行してください。' }
    return New-Row 'signin-adobe' $name $state 'サインインの記録あり' 'なし' $note
}

# ---------------------------------------------------------------- 資格情報マネージャー（汎用資格情報だけ）

# Windows の cmdkey（マイクロソフトのツール）で一覧と削除を行う。
# 以前は資格情報の関数をスクリプトから直接呼んでいたが、パスワードを盗むウイルスと似た動きのため、ウイルス対策に疑われるおそれがあった。
# 出力は日本語・英語の両方に対応（「ターゲット:」「種類: 汎用」／「Target:」「Type: Generic」）。
function Get-CmdkeyGeneric {
    if (-not $env:WINDIR) { return $null }   # 場所を確かめてから組み立てる
    $exe = Join-Path $env:WINDIR 'System32\cmdkey.exe'
    if (-not (Test-Path -LiteralPath $exe)) { return $null }
    $lines = @(& $exe /list 2>$null)
    $list = New-Object System.Collections.Generic.List[string]
    $target = $null
    foreach ($l in $lines) {
        $t = ([string]$l).Trim()
        if ($t -match '^(ターゲット|Target)\s*:\s*(.+)$') { $target = $Matches[2].Trim(); continue }
        if ($target -and $t -match '^(種類|Type)\s*:\s*(.+)$') {
            $kind = $Matches[2].Trim()
            if ($kind -match '汎用|Generic') { $list.Add($target) }   # 「ドメイン パスワード」（ネットワーク共有・プリンター）は残す
            $target = $null
        }
    }
    return ,$list
}

# 消さないもの：Windows 自体のサインインに使う印と、プロファイルの keepCredentials に書いた語を含むもの
function Get-RemovableCredentials {
    param($Prof)
    $all = Get-CmdkeyGeneric
    if ($null -eq $all) { return $null }
    $keep = @('virtualapp/didlogical', 'SSO_POP_Device', 'SSO_POP_User') + @(Get-Pref $Prof 'keepCredentials' @())
    # 「,」を付けて一覧のまま返す（付けないと、0 件のとき PowerShell が $null に変え、点検が「調べられません」になっていた。v0.19.4 の実機で判明）
    return ,@($all | Where-Object {
        $t = $_
        -not ($keep | Where-Object { $_ -and $t.IndexOf([string]$_, [StringComparison]::OrdinalIgnoreCase) -ge 0 })
    })
}

function Test-StoredCredentials {
    param($Prof)
    $name = '保存されたログイン情報（資格情報マネージャー）'
    $c = Get-RemovableCredentials $Prof
    if ($null -eq $c) { return New-Row 'signin-cred' $name 'NA' '調べられません' 'なし' '' }
    $state = 'OK'; if ($c.Count -gt 0) { $state = 'NG' }
    return New-Row 'signin-cred' $name $state ('{0} 件' -f $c.Count) 'なし' 'アプリが保存したログイン（汎用資格情報）。ネットワーク共有・プリンター用の「Windows 資格情報」は対象外。'
}

function Clear-StoredCredentials {
    param($Prof)
    $name = '保存されたログイン情報（資格情報マネージャー）'
    $c = Get-RemovableCredentials $Prof
    if ($null -eq $c) { return New-Row 'signin-cred' $name 'SKIP' '調べられません' 'なし' '' }
    if ($c.Count -eq 0) { return New-Row 'signin-cred' $name 'SAME' '0 件' 'なし' '' }
    $failed = 0
    $exe = Join-Path $env:WINDIR 'System32\cmdkey.exe'
    foreach ($t in $c) {
        & $exe ('/delete:' + $t) 2>$null | Out-Null
        if ($LASTEXITCODE -ne 0) { $failed++ }
    }
    $state = 'DONE'; if ($failed -gt 0) { $state = 'FAIL' }
    return New-Row 'signin-cred' $name $state ('{0} 件' -f $c.Count) 'なし' $(if ($failed) { ('{0} 件は消せませんでした。' -f $failed) } else { '消しました（ネットワーク共有・プリンター用の資格情報は残しています）。' })
}

# ---------------------------------------------------------------- 受講生が入れた可能性のあるアプリ・ほかのブラウザー

# 名前、閉じるプロセス、消すデータの場所（* を含んでよい。そのアプリのログインと設定が消える）
function Get-SignInApps {
    $L = $env:LOCALAPPDATA; $R = $env:APPDATA
    return @(
        @{ Name = 'Firefox';             Proc = @('firefox');                 Paths = @("$R\Mozilla\Firefox", "$L\Mozilla\Firefox") },
        @{ Name = 'Brave';               Proc = @('brave');                   Paths = @("$L\BraveSoftware\Brave-Browser\User Data") },
        @{ Name = 'Opera';               Proc = @('opera');                   Paths = @("$R\Opera Software", "$L\Opera Software") },
        @{ Name = 'Vivaldi';             Proc = @('vivaldi');                 Paths = @("$L\Vivaldi\User Data") },
        @{ Name = 'Zoom';                Proc = @('Zoom');                    Paths = @("$R\Zoom\data") },
        @{ Name = 'LINE';                Proc = @('LINE', 'LineLauncher');    Paths = @("$L\LINE\Data") },
        @{ Name = 'Discord';             Proc = @('Discord');                 Paths = @("$R\discord") },
        @{ Name = 'Slack';               Proc = @('slack');                   Paths = @("$R\Slack") },
        @{ Name = 'Microsoft Teams';     Proc = @('ms-teams', 'Teams');       Paths = @("$L\Packages\MSTeams_8wekyb3d8bbwe\LocalCache", "$R\Microsoft\Teams") },
        @{ Name = 'ChatGPT';             Proc = @('ChatGPT');                 Paths = @("$L\Packages\OpenAI.ChatGPT-Desktop_*\LocalCache", "$R\ChatGPT") },
        @{ Name = 'Claude';              Proc = @('claude');                  Paths = @("$R\Claude") },
        @{ Name = 'Google ドライブ';     Proc = @('GoogleDriveFS');           Paths = @("$L\Google\DriveFS") },
        @{ Name = 'Dropbox';             Proc = @('Dropbox');                 Paths = @("$L\Dropbox", "$R\Dropbox") },
        @{ Name = 'iCloud';              Proc = @('iCloudHome', 'iCloudServices', 'iCloudDrive', 'iCloudPhotos', 'iCloudCKKS'); Paths = @("$L\Packages\AppleInc.iCloud_*\LocalCache", "$L\Packages\AppleInc.iCloud_*\LocalState", "$R\Apple Computer\Preferences") },
        @{ Name = 'WhatsApp';            Proc = @('WhatsApp');                Paths = @("$L\Packages\5319275A.WhatsAppDesktop_*\LocalState") },
        @{ Name = 'Spotify';             Proc = @('Spotify');                 Paths = @("$R\Spotify") },
        @{ Name = 'Notion';              Proc = @('Notion');                  Paths = @("$R\Notion") },
        @{ Name = 'Visual Studio Code（サインイン・最近開いたもの）'; Proc = @('Code'); Paths = @("$R\Code") }
    )
}

function Resolve-AppPaths {
    param($App)
    $out = @()
    foreach ($p in $App.Paths) {
        if ($p -like '*`**') {
            $parent = Split-Path -Parent $p
            $leaf = Split-Path -Leaf $p
            # 「Packages\名前_*\LocalCache」の形：* は 1 段目にしか使わない
            $grand = Split-Path -Parent $parent
            $pattern = Split-Path -Leaf $parent
            if ($pattern -like '*`**' -and (Test-Path -LiteralPath $grand)) {
                foreach ($d in Get-ChildItem -LiteralPath $grand -Directory -Filter $pattern -ErrorAction SilentlyContinue) {
                    $full = Join-Path $d.FullName $leaf
                    if (Test-Path -LiteralPath $full) { $out += $full }
                }
            }
        } elseif (Test-Path -LiteralPath $p) { $out += $p }
    }
    return @($out)
}

function Test-SignInApps {
    param($Prof)
    $name = 'ほかのアプリ・ブラウザーのログイン'
    $found = @()
    foreach ($a in Get-SignInApps) { if ((Resolve-AppPaths $a).Count -gt 0) { $found += $a.Name } }
    $state = 'OK'; if ($found.Count -gt 0) { $state = 'NG' }
    $cur = 'なし'; if ($found.Count -gt 0) { $cur = ($found -join '、') }
    return New-Row 'signin-apps' $name $state $cur 'なし' '使った記録があるもの。［履歴を消す］で、そのアプリのデータ（ログインを含む）を消します。'
}

function Clear-SignInApps {
    param($Prof)
    $name = 'ほかのアプリ・ブラウザーのログイン'
    if (-not [bool](Get-Pref $Prof 'signOutApps' $true)) { return New-Row 'signin-apps' $name 'SKIP' '' '' 'プロファイルで「消さない」になっています。' }
    $done = @(); $failedApps = @()
    foreach ($a in Get-SignInApps) {
        $paths = Resolve-AppPaths $a
        if ($paths.Count -eq 0) { continue }
        foreach ($pn in $a.Proc) { Stop-Browser $pn }
        $items = @($paths | ForEach-Object { Get-Item -LiteralPath $_ -Force -ErrorAction SilentlyContinue })
        $failed = Remove-Items $items
        if ($failed -gt 0) { $failedApps += $a.Name } else { $done += $a.Name }
    }
    if ($done.Count -eq 0 -and $failedApps.Count -eq 0) { return New-Row 'signin-apps' $name 'SAME' 'なし' 'なし' '' }
    $state = 'DONE'; $note = '消したもの：' + ($done -join '、') + '（アプリは閉じました）。'
    if ($failedApps.Count -gt 0) { $state = 'FAIL'; $note = '一部を消せませんでした：' + ($failedApps -join '、') + '（使用中の可能性）。' + $(if ($done.Count) { '消したもの：' + ($done -join '、') } else { '' }) }
    return New-Row 'signin-apps' $name $state (($done + $failedApps) -join '、') 'なし' $note
}

# ---------------------------------------------------------------- 利用者が自分で入れたアプリ（知らせるだけ）

function Test-UserInstalledApps {
    param($Prof)
    $name = 'この利用者が入れたアプリ'
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
    $names = @()
    if (Test-Path -LiteralPath $key) {
        foreach ($k in Get-ChildItem -LiteralPath $key -ErrorAction SilentlyContinue) {
            $dn = $k.GetValue('DisplayName', $null)
            if ($dn -and -not $k.GetValue('SystemComponent', $null)) { $names += [string]$dn }
        }
    }
    $names = @($names | Sort-Object -Unique)
    $cur = 'なし'; if ($names.Count -gt 0) { $cur = ($names -join '、') }
    return New-Row 'userapps' $name 'NA' $cur '' '管理者の権限なしで入れられたアプリ（受講生が入れた可能性）。必要なら「設定 → アプリ」から削除してください。基準の状態に戻すには、まなびとバックアップでの復元が確実です。'
}

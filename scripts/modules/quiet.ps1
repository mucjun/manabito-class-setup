# まなびと講習準備 ― 受講生の気が散るものを出さない（Edge の最初の設定の画面・Windows の広告とおすすめ・Copilot）
#
# 点検（Test-*）とそろえる（Set-*）は、下の同じ一覧を使う（判定と書き込みがずれないように）。
# 書けなかった値は失敗にせず SKIP にして、手で変える方法を案内する（新しい Windows では書き込みを止められることがあるため）。

# 一覧：Path・Name・講習用の値。プロファイルのキー（edgeFirstRun・windowsAds・copilot）が hide のとき、この値にそろえる
$script:QuietSets = @{
    'edgefirstrun' = @{
        Name = 'Edge の最初の設定の画面'; Pref = 'edgeFirstRun'
        Manual = 'Edge を開き、最初の画面の質問に答えて進めてください（受講生ごとに出ます）。'
        # 要素が 1 つの配列は、先頭の「,」が無いと PowerShell が平らにしてしまう
        Values = @(
            ,@('HKLM:\SOFTWARE\Policies\Microsoft\Edge', 'HideFirstRunExperience', 1)
        )
    }
    'windowsads' = @{
        Name = 'Windows の広告・おすすめ・ヒント'; Pref = 'windowsAds'
        Manual = '設定 →「個人用設定」→「スタート」「ロック画面」、設定 →「システム」→「通知」の「追加の設定」で、おすすめ・ヒントをオフにしてください。'
        Values = @(
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SubscribedContent-338388Enabled', 0),  # スタートのおすすめ
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SubscribedContent-338389Enabled', 0),  # ヒントとおすすめ
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SubscribedContent-310093Enabled', 0),  # 更新後の「ようこそ」
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SubscribedContent-338393Enabled', 0),  # 設定アプリのおすすめ
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SubscribedContent-353694Enabled', 0),  # 設定アプリのおすすめ
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SubscribedContent-353696Enabled', 0),  # 設定アプリのおすすめ
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SystemPaneSuggestionsEnabled', 0),
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'SoftLandingEnabled', 0),
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'RotatingLockScreenOverlayEnabled', 0), # ロック画面の豆知識・広告
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\UserProfileEngagement', 'ScoobeSystemSettingEnabled', 0),       # 「Windows を最大限に活用」
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced', 'Start_AccountNotifications', 0)            # スタートのアカウントのお知らせ
        )
    }
    'lockscreen' = @{
        Name = 'ロック画面・サインイン画面の写真'; Pref = 'lockScreenPhoto'
        Manual = '設定 →「個人用設定」→「ロック画面」で、背景を「画像」にし、「サインイン画面にロック画面の背景画像を表示する」をオフにしてください。デスクトップの「この写真について」は、デスクトップの背景を「単色」にすると消えます。'
        Values = @(
            @('HKLM:\SOFTWARE\Policies\Microsoft\Windows\System', 'DisableLogonBackgroundImage', 1),          # サインイン画面は単色（写真を出さない）
            @('HKLM:\SOFTWARE\Policies\Microsoft\Windows\Personalization', 'NoLockScreen', 1),              # ロック画面を出さない（Pro では効かないことがある）
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager', 'RotatingLockScreenEnabled', 0),  # ロック画面のスポットライト（毎日替わる写真）を止める
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons\NewStartPanel', '{2cc5ca98-6485-489a-920e-b3e88a6ccce3}', 1),  # デスクトップの「この写真について」を隠す
            @('HKCU:\Software\Policies\Microsoft\Windows\CloudContent', 'DisableSpotlightCollectionOnDesktop', 1)   # デスクトップのスポットライトを止める
        )
    }
    'quickaccess' = @{
        Name = 'エクスプローラーの「よく使うフォルダー」「最近使ったファイル」の表示'; Pref = 'quickAccessHistory'
        Manual = 'エクスプローラーの「…」→「オプション」→「プライバシー」で、「最近使用したファイルを表示する」「頻繁に使用されるフォルダーを表示する」をオフにしてください。'
        Values = @(
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer', 'ShowFrequent', 0),   # よく使うフォルダーを表示しない
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer', 'ShowRecent', 0)      # 最近使ったファイルを表示しない
        )
    }
    'startapps' = @{
        Name = 'スタートの「最近使用」（最近追加したアプリ・よく使うアプリ）'; Pref = 'startAppsList'
        Manual = '設定 →「個人用設定」→「スタート」で、「最近使用」（または「最近追加したアプリを表示する」「よく使うアプリを表示する」）をオフにしてください。'
        Restart = '再起動のあとに効きます（Microsoft の資料による）。スタートメニューは読み込み直しました。'
        Values = @(
            @('HKLM:\SOFTWARE\Policies\Microsoft\Windows\Explorer', 'HideRecentlyAddedApps', 1),          # 最近追加したアプリを出さない（このアプリ・まなびとバックアップも出ない）
            @('HKCU:\Software\Policies\Microsoft\Windows\Explorer', 'HideRecentlyAddedApps', 1),          # 同じ設定を利用者ごとにも（PC 全体だけでは「おすすめ」に残ることがある。v0.20.2）
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Start', 'ShowRecentList', 0),             # 新しいスタート（「おすすめ」が「最近使用」になった版）の「最近追加したアプリ」。v0.20.4
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Start', 'ShowRecentSection', 0),          # 設定の「新着順」（英語版 Recent）の欄のスイッチ。手でオフにすると消えることを実機で確認。v0.20.5
            @('HKLM:\SOFTWARE\Policies\Microsoft\Windows\Explorer', 'HideRecommendedSection', 1),         # 「最近使用」（旧「おすすめ」）の欄そのものを出さない。v0.20.4
            @('HKCU:\Software\Policies\Microsoft\Windows\Explorer', 'HideRecommendedSection', 1),
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced', 'Start_TrackProgs', 0)   # よく使うアプリを出さない
        )
    }
    'copilot' = @{
        Name = 'Copilot'; Pref = 'copilot'
        Manual = 'タスクバーを右クリック →「タスクバーの設定」で Copilot をオフにしてください。'
        Values = @(
            @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced', 'ShowCopilotButton', 0),
            @('HKCU:\Software\Policies\Microsoft\Windows\WindowsCopilot', 'TurnOffWindowsCopilot', 1)
        )
    }
}

# 一覧のうち、講習用の値になっていないものの数
function Get-QuietMismatch {
    param([string]$Key)
    $set = $script:QuietSets[$Key]
    $bad = @()
    foreach ($v in $set.Values) {
        $cur = Get-RegValue $v[0] $v[1]
        if ($null -eq $cur -or [int]$cur -ne [int]$v[2]) { $bad += ,$v }   # 「,」で 1 組のまま足す（ばらさない）
    }
    return ,$bad
}

function Test-QuietItem {
    param($Prof, [string]$Key)
    $set = $script:QuietSets[$Key]
    $want = [string](Get-Pref $Prof $set.Pref 'hide')
    $bad = Get-QuietMismatch $Key
    $cur = '出さない'
    if ($bad.Count -gt 0) {
        $cur = '出す'
        if ($set.Values.Count -gt 1) { $cur = ('出す（{0} か所）' -f $bad.Count) }
    }
    if ($want -ne 'hide') { return New-Row ('quiet-' + $Key) $set.Name 'NA' $cur '確認のみ' '' }
    $state = 'OK'; if ($bad.Count -gt 0) { $state = 'NG' }
    return New-Row ('quiet-' + $Key) $set.Name $state $cur '出さない' ''
}

function Set-QuietItem {
    param($Prof, [string]$Key)
    $set = $script:QuietSets[$Key]
    $id = 'quiet-' + $Key
    $want = [string](Get-Pref $Prof $set.Pref 'hide')
    if ($want -ne 'hide') { return New-Row $id $set.Name 'SKIP' '' '確認のみ' '' }
    $before = Test-QuietItem $Prof $Key
    if ($before.State -eq 'OK') { return New-Row $id $set.Name 'SAME' $before.Current $before.Expected '' }
    $failed = 0
    if ($Key -eq 'startapps') {
        # スタートメニューの部品は、自分の覚えている値で上書きすることがあるため、止めてから書く（v0.20.5）
        Get-Process -Name StartMenuExperienceHost -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 800
    }
    foreach ($v in (Get-QuietMismatch $Key)) {
        try { Set-RegDword $v[0] $v[1] ([int]$v[2]) } catch { $failed++; Write-Host ('（書けませんでした：' + $v[0] + '\' + $v[1] + '：' + $_.Exception.Message + '）') }
    }
    $script:NeedExplorerRestart = $true
    if ($Key -eq 'startapps') {
        # スタートメニューを読み込み直す（再起動しなくても、なるべくその場で効くように）
        Get-Process -Name StartMenuExperienceHost -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    if ($failed -gt 0) {
        return New-Row $id $set.Name 'SKIP' $before.Current $before.Expected ('{0} か所は、この版の Windows ではアプリから変えられませんでした。{1}' -f $failed, $set.Manual)
    }
    $done = ''
    if ($set.ContainsKey('Restart')) { $done = $set.Restart }
    return New-Row $id $set.Name 'DONE' $before.Current $before.Expected $done
}

function Test-EdgeFirstRun { param($Prof) Test-QuietItem $Prof 'edgefirstrun' }
function Test-WindowsAds   { param($Prof) Test-QuietItem $Prof 'windowsads' }
function Test-Copilot      { param($Prof) Test-QuietItem $Prof 'copilot' }
function Test-LockScreenPhoto { param($Prof) Test-QuietItem $Prof 'lockscreen' }
function Test-QuickAccessShow  { param($Prof) Test-QuietItem $Prof 'quickaccess' }
function Test-StartAppsList    { param($Prof) Test-QuietItem $Prof 'startapps' }
function Set-StartAppsList     { param($Prof) Set-QuietItem $Prof 'startapps' }
function Set-QuickAccessShow   { param($Prof) Set-QuietItem $Prof 'quickaccess' }
function Set-LockScreenPhoto  { param($Prof) Set-QuietItem $Prof 'lockscreen' }
function Set-EdgeFirstRun  { param($Prof) Set-QuietItem $Prof 'edgefirstrun' }
function Set-WindowsAds    { param($Prof) Set-QuietItem $Prof 'windowsads' }
function Set-Copilot       { param($Prof) Set-QuietItem $Prof 'copilot' }

# ---- スタートの「すべて」のビュー（v0.20.3）
# HKCU\Software\Microsoft\Windows\CurrentVersion\Start の AllAppsViewMode：0 = カテゴリ（初めの値）、1 = グリッド、2 = 一覧
# スタートメニューの部品（StartMenuExperienceHost）は、自分の覚えている値で上書きすることがあるため、止めてから書く（次にスタートを開くと読み直す）
$script:StartViewNames = @{ 0 = 'カテゴリ'; 1 = 'グリッド'; 2 = '一覧' }
$script:StartViewWant = @{ 'category' = 0; 'grid' = 1; 'list' = 2 }

function Test-StartAllAppsView {
    param($Prof)
    $name = 'スタートの「すべて」のビュー'
    $want = [string](Get-Pref $Prof 'startAllAppsView' 'category')
    $v = Get-RegValue 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Start' 'AllAppsViewMode'
    $cur = 'カテゴリ（初めの値）'
    if ($null -ne $v) { $cur = $script:StartViewNames[[int]$v]; if (-not $cur) { $cur = ('設定 ' + $v) } }
    if (-not $script:StartViewWant.ContainsKey($want)) { return New-Row 'startview' $name 'NA' $cur '変えない' '' }
    $w = $script:StartViewWant[$want]
    $state = 'NG'; if (($null -eq $v -and $w -eq 0) -or ($null -ne $v -and [int]$v -eq $w)) { $state = 'OK' }
    return New-Row 'startview' $name $state $cur $script:StartViewNames[$w] ''
}

function Set-StartAllAppsView {
    param($Prof)
    $name = 'スタートの「すべて」のビュー'
    $want = [string](Get-Pref $Prof 'startAllAppsView' 'category')
    if (-not $script:StartViewWant.ContainsKey($want)) { return New-Row 'startview' $name 'SKIP' '' '変えない' '' }
    $before = Test-StartAllAppsView $Prof
    if ($before.State -eq 'OK') { return New-Row 'startview' $name 'SAME' $before.Current $before.Expected '' }
    try {
        Get-Process -Name StartMenuExperienceHost -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 800
        Set-RegDword 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Start' 'AllAppsViewMode' ([int]$script:StartViewWant[$want])
    } catch {
        return New-Row 'startview' $name 'SKIP' $before.Current $before.Expected ('変えられませんでした（' + $_.Exception.Message + '）。スタートの「すべて」の右の「ビュー」から選んでください。')
    }
    return New-Row 'startview' $name 'DONE' $before.Current $before.Expected '次にスタートを開いたときから変わります。'
}

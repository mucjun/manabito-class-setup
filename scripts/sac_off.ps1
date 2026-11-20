# まなびと講習準備 ― スマート アプリ コントロールをオフにする
#
# 画面のアプリから「管理者として」呼ばれる:
#   powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File sac_off.ps1
#       -OutFile <結果.tsv> -LogFile <記録.txt>
#
# やり方：VerifiedAndReputablePolicyState を 0（オフ）にして、CiTool があればポリシーを読み直させる。
# Microsoft が公式に案内している方法ではないため、効かなかったときは画面のアプリが
# 「Windows セキュリティ」の画面を開いて、手でオフにしてもらう。
# 注意：一度オフにすると、Windows の版によっては初期化するまでオンに戻せない。

param(
    [Parameter(Mandatory = $true)][string]$OutFile,
    [string]$LogFile = ''
)

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($LogFile) { try { Start-Transcript -LiteralPath $LogFile -Force | Out-Null } catch { } }
. (Join-Path $here 'modules\common.ps1')
. (Join-Path $here 'modules\set.ps1')

$key = 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy'
$name = 'VerifiedAndReputablePolicyState'
$names = @{ 0 = 'オフ'; 1 = 'オン'; 2 = '評価中' }

function Get-StateText {
    param($v)
    if ($null -eq $v) { return 'この Windows には無い' }
    if ($names.ContainsKey([int]$v)) { return $names[[int]$v] }
    return [string]$v
}

$before = Get-RegValue $key $name
$state = 'FAIL'
$detail = ''

if (-not (Test-IsAdmin)) {
    $detail = '管理者として動いていないため、変えられませんでした。'
} elseif ($null -eq $before) {
    $state = 'SKIP'
    $detail = 'この Windows にはスマート アプリ コントロールがありません。'
} elseif ($before -eq 0) {
    $state = 'SAME'
    $detail = 'もともとオフでした。'
} else {
    try {
        Set-RegDword $key $name 0
        $citool = Join-Path $env:WINDIR 'System32\CiTool.exe'
        if (Test-Path -LiteralPath $citool) {
            $out = & $citool --refresh 2>&1
            Write-Output ('CiTool --refresh: ' + (($out | ForEach-Object { [string]$_ }) -join ' '))
        }
        $after = Get-RegValue $key $name
        if ($after -eq 0) {
            $state = 'DONE'
            $detail = '確実に反映させるため、PC を再起動してください。'
        } else {
            $detail = ('書き込んだあとも {0} のままでした。' -f (Get-StateText $after))
        }
    } catch {
        $detail = ('変えられませんでした：' + $_.Exception.Message)
    }
}

$after = Get-RegValue $key $name
$line = (@('スマート アプリ コントロール', 'sac', 'スマート アプリ コントロール', $state,
           (Get-StateText $before), (Get-StateText $after), $detail) | ForEach-Object { ([string]$_) -replace "[`t`r`n]+", ' ' }) -join "`t"
$utf8 = New-Object System.Text.UTF8Encoding $true
$tmp = $OutFile + '.part'
[System.IO.File]::WriteAllLines($tmp, [string[]]@("#区分`tID`t項目`t結果`t変える前`t変えた後`t説明", $line), $utf8)
Move-Item -LiteralPath $tmp -Destination $OutFile -Force
Write-Output $line
if ($LogFile) { try { Stop-Transcript | Out-Null } catch { } }
exit 0

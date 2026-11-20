# 開発用の確認：部品（modules\*.ps1）が $script: に置く変数と、
# 呼び出す側のスクリプト（scripts\*.ps1）の引数が同じ名前になっていないかを調べる。
# 同じ名前だと、部品を読み込んだときに引数の値が消える（v0.4.0 の［履歴を消す］の不具合）。
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$moduleVars = @{}
foreach ($f in Get-ChildItem (Join-Path $root 'scripts/modules') -Filter *.ps1) {
    foreach ($m in [regex]::Matches((Get-Content $f.FullName -Raw), '\$script:(\w+)\s*=')) {
        $moduleVars[$m.Groups[1].Value.ToLower()] = $f.Name
    }
}
$bad = 0
foreach ($f in Get-ChildItem (Join-Path $root 'scripts') -Filter *.ps1) {
    $t = $null; $e = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$t, [ref]$e)
    if ($null -eq $ast.ParamBlock) { continue }
    foreach ($p in $ast.ParamBlock.Parameters) {
        $n = $p.Name.VariablePath.UserPath.ToLower()
        if ($moduleVars.ContainsKey($n)) {
            Write-Output ('NG: {0} の引数 -{1} と {2} の $script:{1} が重なっています' -f $f.Name, $p.Name.VariablePath.UserPath, $moduleVars[$n])
            $bad++
        }
    }
}
if ($bad -eq 0) { Write-Output 'OK: 引数と部品の変数の重なりはありません' }
exit $bad

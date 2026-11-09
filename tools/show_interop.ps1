$a = [Reflection.Assembly]::LoadFrom((Resolve-Path 'C:\Users\Rose\Projects\OpenDepot\tools\Interop.NEXTGENLib.dll'))
$t = $a.GetType('NEXTGENLib.IRcmpBundler')
Write-Output "=== IRcmpBundler methods (auth/key) ==="
$t.GetMethods() | Where-Object { $_.Name -match 'Auth|Key|Password|DateCode|Open|ProgramMode|Unlock|Lock|Reset' } | ForEach-Object {
    $ps = ($_.GetParameters() | ForEach-Object { "$($_.ParameterType.Name) $($_.Name)" }) -join ', '
    Write-Output ("{0} {1}({2})" -f $_.ReturnType.Name, $_.Name, $ps)
}
Write-Output "=== IRcmpBundler all methods ==="
$t.GetMethods() | ForEach-Object {
    $ps = ($_.GetParameters() | ForEach-Object { "$($_.ParameterType.Name) $($_.Name)" }) -join ', '
    Write-Output ("{0} {1}({2})" -f $_.ReturnType.Name, $_.Name, $ps)
}

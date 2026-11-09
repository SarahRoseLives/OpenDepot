$ErrorActionPreference = 'Stop'
$rdk = "C:\Program Files\Common Files\Motorola\RDK\ADK7_2_SP04_SR13"
$backup = "$env:TEMP\authAES_rdk_backup.dll"
$proxy = "C:\Users\Rose\Projects\OpenDepot\build\out\proxy_rdk\authAES.dll"
$dep = "C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot"
$cpg = "C:\Users\Rose\Projects\OpenDepot\build\radio-backups\prewrite-q387.cpg"
$tap = "C:\Users\Rose\AppData\Local\Temp\opencode\tap6.py"
$tlog = "C:\Users\Rose\AppData\Local\Temp\opencode\tap6.log"
$plog = "$env:TEMP\authaes_proxy.log"

if (-not (Test-Path $backup)) { Copy-Item "$rdk\authAES.dll" $backup -Force }
if ((Get-Item $backup).Length -ne 61952) { Write-Host "WARNING: backup size unexpected: $((Get-Item $backup).Length)" }

Remove-Item $plog -ErrorAction SilentlyContinue
Copy-Item $proxy "$rdk\authAES.dll" -Force
Write-Host "Logging proxy installed in RDK."

Get-Process PatPortDepot -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process python -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -match 'tap6' } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

Start-Process python -ArgumentList @($tap, "300", $tlog) -WindowStyle Hidden
Start-Sleep -Seconds 2
Start-Process "$dep\PatPortDepot.exe" -ArgumentList ('"' + $cpg + '"') -WorkingDirectory $dep
Write-Host ""
Write-Host "Depot started. In the depot window:"
Write-Host "  1) wait for prewrite-q387.cpg to load"
Write-Host "  2) Tools -> Depot -> Write Codeplug"
Write-Host "  3) select COM21, confirm"
Write-Host ""
Write-Host "Waiting up to 240s for the auth capture..."

$deadline = (Get-Date).AddSeconds(240)
$got = $false
while ((Get-Date) -lt $deadline) {
    if (Test-Path $plog) {
        $c = Get-Content $plog -Raw -ErrorAction SilentlyContinue
        if ($c -match 'ENC out|DEC out') { $got = $true; break }
    }
    Start-Sleep -Seconds 2
}

Get-Process PatPortDepot -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Copy-Item $backup "$rdk\authAES.dll" -Force
Write-Host "Original authAES.dll restored."

if ($got -and (Test-Path $plog)) {
    Write-Host "=== CAPTURED AUTH ==="
    Get-Content $plog
} else {
    Write-Host "No auth capture. Log (if any):"
    if (Test-Path $plog) { Get-Content $plog }
}

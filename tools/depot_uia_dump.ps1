param(
    [string]$File = "C:\Users\Rose\Projects\OpenDepot\build\out\zone-edit.cpg",
    [int]$WaitSeconds = 8
)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$exe = "C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
$dir = Split-Path -Parent $exe
$args = if ($File) { "`"$File`"" } else { "" }
$p = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $WaitSeconds
$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
$win = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $cond)
if (-not $win) { Write-Output "no UIA window"; if(-not $p.HasExited){Stop-Process -Id $p.Id -Force}; exit 1 }
Write-Output ("window name='" + $win.Current.Name + "' class=" + $win.Current.ClassName)
$all = $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
Write-Output ("elements=" + $all.Count)
foreach ($e in $all) {
    try {
        $ct = $e.Current.ControlType.ProgrammaticName
        $nm = $e.Current.Name
        $aid = $e.Current.AutomationId
        $cls = $e.Current.ClassName
        if ($ct -match 'Menu|Button|ToolBar|Tab|Tree|ComboBox|Edit' -and ($nm -or $aid)) {
            Write-Output ("{0,-28} name='{1}' aid='{2}' class='{3}'" -f $ct, $nm, $aid, $cls)
        }
    } catch {}
}
if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2; $p.Refresh(); if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }

$ErrorActionPreference = 'Continue'
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$report = Join-Path $env:TEMP 'chatterino-clean-cycle-report.txt'
"TIME=$(Get-Date -Format o)" | Out-File $report

Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3

function Test-Launch([string]$label) {
  $before = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
  $p = Start-Process -FilePath $edge -ArgumentList 'about:blank' -PassThru
  Start-Sleep -Seconds 45
  $after = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
  $new = $after | Where-Object { $_ -notin $before }
  "$label : host=$($new -join ',')" | Out-File $report -Append
  $sp = Get-Content "$env:LOCALAPPDATA\Microsoft\Edge\User Data\Default\Secure Preferences" -Raw | ConvertFrom-Json
  "$label : bogfpdfo=$($null -ne $sp.extensions.settings.'bogfpdfoagkaebimmlcbgmfmanhbhhlm') entries=$($sp.extensions.settings.PSObject.Properties.Name.Count)" | Out-File $report -Append
  return $new.Count -gt 0
}

# Cycle 1: launch, CLEAN close (graceful), relaunch
$null = Test-Launch 'launch1'
$procs = Get-Process msedge -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 }
foreach ($pr in $procs) { $null = $pr.CloseMainWindow() }
Start-Sleep -Seconds 15
Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
$ok2 = Test-Launch 'launch2_after_clean_close'
"launch2 loaded: $ok2" | Out-File $report -Append

Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
"DONE" | Out-File $report -Append
Get-Content $report
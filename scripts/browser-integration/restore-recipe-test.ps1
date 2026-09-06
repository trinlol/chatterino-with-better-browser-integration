$ErrorActionPreference = 'Continue'
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$userData = "$env:LOCALAPPDATA\Microsoft\Edge\User Data"
$report = Join-Path $env:TEMP 'chatterino-restore-recipe.txt'
"TIME=$(Get-Date -Format o)" | Out-File $report

Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3

# Step A: launch, open edge://extensions via CDP, then force-kill (mimic pre-06:30 state)
$p1 = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$userData", '--profile-directory=Default',
  '--no-first-run','--no-default-browser-check', '--remote-debugging-port=9333',
  'about:blank'
) -PassThru
Start-Sleep -Seconds 20
try {
  $null = Invoke-WebRequest -Uri 'http://127.0.0.1:9333/json/new?edge://extensions' -UseBasicParsing -TimeoutSec 5
  "opened extensions tab via /json/new" | Out-File $report -Append
} catch { "json/new failed: $($_.Exception.Message)" | Out-File $report -Append }
Start-Sleep -Seconds 8
Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3

# Step B: relaunch about:blank; does session restore the extensions tab? does the extension load?
$beforeLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$p2 = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$userData", '--profile-directory=Default',
  '--no-first-run','--no-default-browser-check', '--remote-debugging-port=9334',
  'about:blank'
) -PassThru
Start-Sleep -Seconds 40
$r = $null
try { $r = Invoke-WebRequest -Uri 'http://127.0.0.1:9334/json/list' -UseBasicParsing -TimeoutSec 5 } catch {}
if ($r) {
  $targets = $r.Content | ConvertFrom-Json
  "STEP_B targets:" | Out-File $report -Append
  $targets | ForEach-Object { "  [$($_.type)] $($_.url)" | Out-File $report -Append }
} else { "STEP_B CDP unreachable" | Out-File $report -Append }
$afterLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$new = $afterLogs | Where-Object { $_ -notin $beforeLogs }
"STEP_B new host logs: $($new -join ', ')" | Out-File $report -Append

Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
"DONE" | Out-File $report -Append
Write-Output "REPORT=$report"
$ErrorActionPreference = 'Continue'
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$userData = "$env:LOCALAPPDATA\Microsoft\Edge\User Data"
$ghostbuster = "$env:TEMP\chatterino-ghostbuster"
$report = Join-Path $env:TEMP 'chatterino-ghostbuster-report.txt'
"TIME=$(Get-Date -Format o)" | Out-File $report

Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
if (Get-Process msedge -ErrorAction SilentlyContinue) { "ABORT: msedge running" | Out-File $report -Append; exit 2 }

function Get-Ghosts($sp) {
  $sp.extensions.settings.PSObject.Properties |
    Where-Object { $_.Value.location -eq 4 -and $_.Name -ne 'bogfpdfoagkaebimmlcbgmfmanhbhhlm' } |
    ForEach-Object { "$($_.Name)|$($_.Value.path)|disable=$($_.Value.disable_reasons -join ',')" }
}
$spRaw = Get-Content "$userData\Default\Secure Preferences" -Raw
"BEFORE ghosts:" | Out-File $report -Append
Get-Ghosts ($spRaw | ConvertFrom-Json) | Out-File $report -Append

$beforeLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name

# 1) Launch real Edge with the ghostbuster loaded via --load-extension (session-scoped)
$p1 = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$userData",
  '--profile-directory=Default',
  '--no-first-run','--no-default-browser-check',
  "--load-extension=$ghostbuster",
  'about:blank'
) -PassThru
"GHOSTBUSTER_PID=$($p1.Id)" | Out-File $report -Append
Start-Sleep -Seconds 25
Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3

$spRaw2 = Get-Content "$userData\Default\Secure Preferences" -Raw
"AFTER GHOSTBUSTER ghosts:" | Out-File $report -Append
Get-Ghosts ($spRaw2 | ConvertFrom-Json) | Out-File $report -Append

# 2) Plain relaunch - auto-load check
$p2 = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$userData",
  '--profile-directory=Default',
  '--no-first-run','--no-default-browser-check',
  'about:blank'
) -PassThru
"RELAUNCH_PID=$($p2.Id)" | Out-File $report -Append
Start-Sleep -Seconds 35

$afterLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$newLogs = $afterLogs | Where-Object { $_ -notin $beforeLogs }
"NATIVE_HOST_NEW_LOGS: $($newLogs -join ', ')" | Out-File $report -Append
foreach ($nl in $newLogs) {
  "--- $nl ---" | Out-File $report -Append
  Get-Content "$env:TEMP\$nl" -ErrorAction SilentlyContinue | Select-Object -First 12 | Out-File $report -Append
}

$spRaw3 = Get-Content "$userData\Default\Secure Preferences" -Raw
$e = ($spRaw3 | ConvertFrom-Json).extensions.settings.'bogfpdfoagkaebimmlcbgmfmanhbhhlm'
if ($e) { "bogfpdfo after: location=$($e.location) disable=$($e.disable_reasons -join ',') last_update=$($e.last_update_time)" | Out-File $report -Append }
else { "bogfpdfo after: ABSENT!" | Out-File $report -Append }

Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
"DONE" | Out-File $report -Append
Write-Output "REPORT=$report"
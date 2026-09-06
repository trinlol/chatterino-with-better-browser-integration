$ErrorActionPreference = 'Continue'
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$userData = "$env:LOCALAPPDATA\Microsoft\Edge\User Data"
$report = Join-Path $env:TEMP 'chatterino-ghost-cleanup-report.txt'
"TIME=$(Get-Date -Format o)" | Out-File $report

# Sanity: Edge must be fully closed
$procs = Get-Process msedge -ErrorAction SilentlyContinue
if ($procs) { "ABORT: msedge still running ($($procs.Count) procs)" | Out-File $report -Append; exit 2 }
"EDGE_CONFIRMED_CLOSED" | Out-File $report -Append

# Snapshot prefs before
function Get-Ghosts($sp) {
  $sp.extensions.settings.PSObject.Properties |
    Where-Object { $_.Value.location -eq 4 -and $_.Name -ne 'bogfpdfoagkaebimmlcbgmfmanhbhhlm' } |
    ForEach-Object { "$($_.Name)|$($_.Value.path)|disable=$($_.Value.disable_reasons -join ',')" }
}
$spRaw = Get-Content "$userData\Default\Secure Preferences" -Raw
"BEFORE ghosts:" | Out-File $report -Append
Get-Ghosts ($spRaw | ConvertFrom-Json) | Out-File $report -Append

# Snapshot native-host logs before (auto-load signal)
$beforeLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$beforeStorage = Test-Path "$userData\Default\Local Extension Settings\bogfpdfoagkaebimmlcbgmfmanhbhhlm\LOG"

# 1) Uninstall the two ghost registrations via Edge's own write path
$p1 = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$userData", '--profile-directory=Default',
  '--no-first-run','--no-default-browser-check',
  '--uninstall-extension=oenpbjpibkeomkimhpldpmabdblmipoa',
  '--uninstall-extension=boiehajcdnhbdmebpnbihmmdfafihlkl',
  'about:blank'
) -PassThru
"UNINSTALL_PID=$($p1.Id)" | Out-File $report -Append
Start-Sleep -Seconds 18

# Kill this uninstall instance only
Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue |
  Where-Object { $_.CommandLine -like '*--uninstall-extension*' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 3

$spRaw2 = Get-Content "$userData\Default\Secure Preferences" -Raw
"AFTER UNINSTALL ghosts:" | Out-File $report -Append
Get-Ghosts ($spRaw2 | ConvertFrom-Json) | Out-File $report -Append

# 2) Plain relaunch - does the extension auto-load now?
$p2 = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$userData", '--profile-directory=Default',
  '--no-first-run','--no-default-browser-check',
  'about:blank'
) -PassThru
"RELAUNCH_PID=$($p2.Id)" | Out-File $report -Append
Start-Sleep -Seconds 30

$afterLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$newLogs = $afterLogs | Where-Object { $_ -notin $beforeLogs }
"NATIVE_HOST_NEW_LOGS: $($newLogs -join ', ')" | Out-File $report -Append
foreach ($nl in $newLogs) {
  "--- $nl content ---" | Out-File $report -Append
  Get-Content "$env:TEMP\$nl" -ErrorAction SilentlyContinue | Select-Object -First 15 | Out-File $report -Append
}
$afterStorage = Test-Path "$userData\Default\Local Extension Settings\bogfpdfoagkaebimmlcbgmfmanhbhhlm\LOG"
"STORAGE_LOG present before=$beforeStorage after=$afterStorage" | Out-File $report -Append

$spRaw3 = Get-Content "$userData\Default\Secure Preferences" -Raw
$e = ($spRaw3 | ConvertFrom-Json).extensions.settings.'bogfpdfoagkaebimmlcbgmfmanhbhhlm'
"bogfpdfo after relaunch: location=$($e.location) disable=$($e.disable_reasons -join ',') last_update=$($e.last_update_time)" | Out-File $report -Append

# 3) Kill the relaunch instance, leave Edge closed
Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue |
  Where-Object { $_.CommandLine -like '*chatterino*' -or $_.CommandLine -like '*about:blank*' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 2
Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
"DONE" | Out-File $report -Append
"REPORT=$report"
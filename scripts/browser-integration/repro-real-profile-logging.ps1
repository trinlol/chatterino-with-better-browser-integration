$ErrorActionPreference = 'Continue'

# 1) Backup real profile state (non-destructive snapshot)
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $env:TEMP ("chatterino-real-profile-backup-" + $stamp)
New-Item -ItemType Directory -Path $backup | Out-Null
$userData = "$env:LOCALAPPDATA\Microsoft\Edge\User Data"
Copy-Item "$userData\Local State" "$backup\Local State" -Force
Copy-Item "$userData\Default\Secure Preferences" "$backup\Secure Preferences" -Force
Copy-Item "$userData\Default\Preferences" "$backup\Preferences" -Force
Write-Output "BACKUP=$backup"

# 2) Wait for the user to fully close Edge (up to 10 min)
$deadline = (Get-Date).AddMinutes(10)
while ((Get-Date) -lt $deadline) {
  $procs = Get-Process msedge -ErrorAction SilentlyContinue
  if (-not $procs) { Write-Output 'EDGE_FULLY_CLOSED'; break }
  Start-Sleep -Seconds 3
}
if ((Get-Date) -ge $deadline) { Write-Output 'EDGE_CLOSE_TIMEOUT'; exit 2 }

# Snapshot native host logs + prefs before diagnostic run
$beforeLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$spBefore = Get-Content "$userData\Default\Secure Preferences" -Raw

# 3) Launch the REAL profile with logging enabled
Start-Sleep -Seconds 2
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$p = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$userData",
  '--profile-directory=Default',
  '--no-first-run','--no-default-browser-check',
  '--enable-logging','--v=1',
  'about:blank'
) -PassThru
Write-Output "DIAG_PID=$($p.Id)"
Start-Sleep -Seconds 25

# 4) Read the startup log
$report = Join-Path $env:TEMP 'chatterino-real-profile-diagnostic-report.txt'
$logPath = "$userData\chrome_debug.log"
"REPORT_TIME=$(Get-Date -Format o)" | Out-File $report
"LOG=$logPath exists=$(Test-Path $logPath)" | Out-File $report -Append
if (Test-Path $logPath) {
  $content = Get-Content $logPath
  "LOG_MTIME=$((Get-Item $logPath).LastWriteTime)" | Out-File $report -Append
  "--- extension_registrar / AddExtension / user-extension lines ---" | Out-File $report -Append
  $content | Select-String -Pattern 'AddExtension|AddComponentExtension|OnExtensionLoaded|OnExtensionFailed|LoadExtension' |
    ForEach-Object { $_.Line } | Select-Object -First 120 | Out-File $report -Append
  "--- lines mentioning our ids or unpacked/failed/corrupt ---" | Out-File $report -Append
  $content | Select-String -Pattern 'bogfpdfo|oenpbjp|boieha|twitch-predictions|Failed to load extension|unpacked|Unpacked|corrupt|verif|signature|malformed|permission denied|extension error' |
    ForEach-Object { $_.Line } | Select-Object -First 120 | Out-File $report -Append
  "--- script_context ids (non-empty) ---" | Out-File $report -Append
  $content | Select-String -Pattern 'extension id:' | ForEach-Object { $_.Line } |
    Where-Object { $_ -match '[a-p]{32}' } | Sort-Object -Unique | Out-File $report -Append
}
Write-Output "REPORT=$report"

# 5) Post-state: prefs after diagnostic run
$spAfter = Get-Content "$userData\Default\Secure Preferences" -Raw
"--- secure-prefs bogfpdfo entry BEFORE vs AFTER ---" | Out-File $report -Append
foreach ($pair in @(@('BEFORE',$spBefore),@('AFTER',$spAfter))) {
  $label = $pair[0]; $raw = $pair[1]
  $obj = $raw | ConvertFrom-Json
  $e = $obj.extensions.settings.'bogfpdfoagkaebimmlcbgmfmanhbhhlm'
  if ($e) {
    "  $label location=$($e.location) last_update=$($e.last_update_time) path_exists=$(Test-Path $e.path)" | Out-File $report -Append
  } else { "  $label bogfpdfo ABSENT" | Out-File $report -Append }
  $ghosts = $obj.extensions.settings.PSObject.Properties | Where-Object { $_.Value.location -eq 4 -and $_.Name -ne 'bogfpdfoagkaebimmlcbgmfmanhbhhlm' }
  foreach ($g in $ghosts) {
    "  $label ghost $($g.Name) path=$($g.Value.path) path_exists=$(Test-Path $g.Value.path)" | Out-File $report -Append
  }
}
$afterLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$newLogs = $afterLogs | Where-Object { $_ -notin $beforeLogs }
"--- new native host logs during diagnostic: $($newLogs -join ', ')" | Out-File $report -Append

# 6) Kill ONLY the diagnostic instance (command line contains --enable-logging)
Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue |
  Where-Object { $_.CommandLine -like '*--enable-logging*' -and $_.CommandLine -like '*profile-directory=Default*' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Write-Output 'DIAGNOSTIC_INSTANCE_KILLED'
Write-Output 'CYCLE_COMPLETE'
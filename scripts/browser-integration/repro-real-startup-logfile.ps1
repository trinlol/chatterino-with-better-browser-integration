$ErrorActionPreference = 'Continue'
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$userData = "$env:LOCALAPPDATA\Microsoft\Edge\User Data"
$logFile = Join-Path $env:TEMP 'chatterino-real-startup.log'
$report = Join-Path $env:TEMP 'chatterino-real-startup-report.txt'
Remove-Item $logFile -Force -ErrorAction SilentlyContinue

# Ensure Edge is closed
Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
$procs = Get-Process msedge -ErrorAction SilentlyContinue
if ($procs) { "ABORT: msedge still running" | Out-File $report; exit 2 }

$beforeLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name

# Launch real profile with explicit log file
$p = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$userData",
  '--no-first-run','--no-default-browser-check',
  '--enable-logging','--v=1',
  "--log-file=$logFile",
  'about:blank'
) -PassThru
"LAUNCH_PID=$($p.Id)" | Out-File $report
Start-Sleep -Seconds 30

"LOG_EXISTS=$(Test-Path $logFile)" | Out-File $report -Append
if (Test-Path $logFile) {
  "LOG_SIZE=$((Get-Item $logFile).Length)" | Out-File $report -Append
  $content = Get-Content $logFile
  "--- lines with our ids / unpacked / load errors / extension registrar ---" | Out-File $report -Append
  $content | Select-String -Pattern 'bogfpdfo|oenpbjp|boieha|twitch-predictions|Failed to load extension|unpacked|Unpacked|corrupt|verif|signature|malformed|permission denied|Extension error|AddExtension|OnExtensionLoaded|extension_registrar.cc:63[3579]' |
    ForEach-Object { $_.Line } | Select-Object -First 200 | Out-File $report -Append
}

$afterLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$newLogs = $afterLogs | Where-Object { $_ -notin $beforeLogs }
"NATIVE_HOST_NEW_LOGS: $($newLogs -join ', ')" | Out-File $report -Append

# Kill all Edge
Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
"DONE" | Out-File $report -Append
"REPORT=$report"
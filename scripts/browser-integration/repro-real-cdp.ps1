$ErrorActionPreference = 'Continue'
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$userData = "$env:LOCALAPPDATA\Microsoft\Edge\User Data"
$logFile = Join-Path $env:TEMP 'chatterino-real-cdp.log'
$dumpOut = Join-Path $env:TEMP 'chatterino-cdp-dump.json'
Remove-Item $logFile -Force -ErrorAction SilentlyContinue
Remove-Item $dumpOut -Force -ErrorAction SilentlyContinue

Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
if (Get-Process msedge -ErrorAction SilentlyContinue) { Write-Output 'ABORT: msedge running'; exit 2 }

$beforeLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name

$p = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$userData",
  '--profile-directory=Default',
  '--no-first-run','--no-default-browser-check',
  '--remote-debugging-port=9333',
  '--enable-logging','--v=1',
  "--log-file=$logFile",
  'about:blank'
) -PassThru
"LAUNCH_PID=$($p.Id)" | Out-File (Join-Path $env:TEMP 'chatterino-cdp-run.txt')
Start-Sleep -Seconds 45

# Try to reach the debugging port
$reachable = $false
try {
  $r = Invoke-WebRequest -Uri 'http://127.0.0.1:9333/json/version' -UseBasicParsing -TimeoutSec 5
  $reachable = $true
  "CDP_REACHABLE: $($r.Content)" | Out-File (Join-Path $env:TEMP 'chatterino-cdp-run.txt') -Append
} catch {
  "CDP_UNREACHABLE: $($_.Exception.Message)" | Out-File (Join-Path $env:TEMP 'chatterino-cdp-run.txt') -Append
}

if ($reachable) {
  node "$PSScriptRoot\dump-extension-state.mjs" 9333 $dumpOut 2>&1 | Out-File (Join-Path $env:TEMP 'chatterino-cdp-run.txt') -Append
}

$afterLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$newLogs = $afterLogs | Where-Object { $_ -notin $beforeLogs }
"NATIVE_HOST_NEW_LOGS: $($newLogs -join ', ')" | Out-File (Join-Path $env:TEMP 'chatterino-cdp-run.txt') -Append

Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
"RUN_COMPLETE" | Out-File (Join-Path $env:TEMP 'chatterino-cdp-run.txt') -Append
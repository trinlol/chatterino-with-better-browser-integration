$ErrorActionPreference = 'Continue'
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$userData = "$env:LOCALAPPDATA\Microsoft\Edge\User Data"
$dumpOut = Join-Path $env:TEMP 'chatterino-cdp-dump.json'
Remove-Item $dumpOut -Force -ErrorAction SilentlyContinue

Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
if (Get-Process msedge -ErrorAction SilentlyContinue) { Write-Output 'ABORT: msedge running'; exit 2 }

$p = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$userData",
  '--profile-directory=Default',
  '--no-first-run','--no-default-browser-check',
  '--remote-debugging-port=9333',
  'edge://extensions/'
) -PassThru
"LAUNCH_PID=$($p.Id)" | Out-File (Join-Path $env:TEMP 'chatterino-cdp2-run.txt')
Start-Sleep -Seconds 40

$reachable = $false
try {
  $r = Invoke-WebRequest -Uri 'http://127.0.0.1:9333/json/version' -UseBasicParsing -TimeoutSec 5
  $reachable = $true
  "CDP_REACHABLE" | Out-File (Join-Path $env:TEMP 'chatterino-cdp2-run.txt') -Append
} catch {
  "CDP_UNREACHABLE: $($_.Exception.Message)" | Out-File (Join-Path $env:TEMP 'chatterino-cdp2-run.txt') -Append
}
if ($reachable) {
  node "$PSScriptRoot\dump-extension-state.mjs" 9333 $dumpOut 2>&1 | Out-File (Join-Path $env:TEMP 'chatterino-cdp2-run.txt') -Append
}
Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
"RUN_COMPLETE" | Out-File (Join-Path $env:TEMP 'chatterino-cdp2-run.txt') -Append
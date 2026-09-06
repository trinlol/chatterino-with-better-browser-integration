$ErrorActionPreference = 'Continue'

$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$userData = "$env:LOCALAPPDATA\Microsoft\Edge\User Data"
$realProfile = "$userData\Default"

$replica = Join-Path $env:TEMP ('chatterino-replica-direct-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $replica | Out-Null
Copy-Item "$realProfile\Secure Preferences" "$replica\Secure Preferences" -Force
Copy-Item "$realProfile\Preferences" "$replica\Preferences" -Force
Copy-Item "$userData\Local State" "$replica\Local State" -Force
Write-Host "replica: $replica"

# Pre-launch host-log snapshot
$beforeLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name

$p = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$replica",
  '--no-first-run','--no-default-browser-check',
  '--enable-logging','--v=1',
  'edge://extensions/'
) -PassThru
Write-Host "edge pid: $($p.Id)"
Start-Sleep -Seconds 14

# Signals
$afterLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$newLogs = $afterLogs | Where-Object { $_ -notin $beforeLogs }
Write-Host "new chatterino native host logs: $($newLogs -join ', ')"

$extLog = Join-Path $replica 'chrome_debug.log'
Write-Host "chrome_debug.log exists: $(Test-Path $extLog)"
if (Test-Path $extLog) {
  $content = Get-Content $extLog
  Write-Host ''
  Write-Host '--- user extension registrations (non-component) ---'
  $content | Select-String -Pattern 'extension_registrar.cc:63[3579]|AddExtension|OnExtensionLoaded|LoadExtension|extension id: *[a-p]' |
    ForEach-Object { $_.Line } | Select-Object -First 40
  Write-Host ''
  Write-Host '--- script_context with real extension ids ---'
  $content | Select-String -Pattern 'extension id:' | ForEach-Object { $_.Line } |
    Where-Object { $_ -notmatch '\(none\)' -and $_.Trim() -ne 'extension id:' } | Select-Object -First 15
}

# Kill edge processes for this profile
Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue |
  Where-Object { $_.CommandLine -like "*$replica*" } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }

Write-Host "KEEP_REPLICA=$replica"
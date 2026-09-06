$ErrorActionPreference = 'Continue'
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$twin = "$env:TEMP\chatterino-twin-490c2c1d"
$report = Join-Path $env:TEMP 'chatterino-twin2-report.txt'
"TIME=$(Get-Date -Format o)" | Out-File $report

Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3

# The twin's prefs were wiped by the earlier tamper detection - rebuild it fresh from real
Remove-Item $twin -Recurse -Force -ErrorAction SilentlyContinue
$excludeDirNames = @('Cache','Code Cache','GPUCache','GrShaderCache','DawnGraphiteCache','DawnWebGPUCache','ShaderCache','GraphiteDawnCache','Crashpad','component_crx_cache','extensions_crx_cache')
$userData = "$env:LOCALAPPDATA\Microsoft\Edge\User Data"
New-Item -ItemType Directory -Path $twin | Out-Null
Get-ChildItem $userData | ForEach-Object {
  if ($_.PSIsContainer) {
    if ($excludeDirNames -contains $_.Name) { return }
    Copy-Item $_.FullName (Join-Path $twin $_.Name) -Recurse -Force -ErrorAction SilentlyContinue
  } else {
    if ($_.Name -in @('LOCK','lockfile')) { return }
    Copy-Item $_.FullName (Join-Path $twin $_.Name) -Force -ErrorAction SilentlyContinue
  }
}
"twin rebuilt" | Out-File $report -Append

$logFile = "$env:TEMP\chatterino-twin-startup.log"
Remove-Item $logFile -Force -ErrorAction SilentlyContinue
$beforeLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name

$p = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$twin", '--profile-directory=Default',
  '--no-first-run','--no-default-browser-check',
  '--enable-logging','--v=1', "--log-file=$logFile",
  'about:blank'
) -PassThru
Start-Sleep -Seconds 50
$afterLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$new1 = $afterLogs | Where-Object { $_ -notin $beforeLogs }
"twin host logs: $($new1 -join ', ')" | Out-File $report -Append
Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

"LOG_SIZE=$((Get-Item $logFile).Length)" | Out-File $report -Append
"=== lines: bogfpdfo/extension load decisions ===" | Out-File $report -Append
Select-String -Path $logFile -Pattern 'bogfpdfo|oenpbjp|boiehaj' | Select-Object -First 25 | ForEach-Object { $_.Line | Out-File $report -Append }
Get-Content $report
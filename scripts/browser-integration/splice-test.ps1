$ErrorActionPreference = 'Continue'
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$userData = "$env:LOCALAPPDATA\Microsoft\Edge\User Data"
$twin = "$env:TEMP\chatterino-twin-490c2c1d"
$report = Join-Path $env:TEMP 'chatterino-splice-report.txt'
"TIME=$(Get-Date -Format o)" | Out-File $report

Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3

# Rebuild pristine twin
Remove-Item $twin -Recurse -Force -ErrorAction SilentlyContinue
$excludeDirNames = @('Cache','Code Cache','GPUCache','GrShaderCache','DawnGraphiteCache','DawnWebGPUCache','ShaderCache','GraphiteDawnCache','Crashpad','component_crx_cache','extensions_crx_cache')
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

# Byte-surgical splice: remove ONLY oenpbjp (same-path ghost)
$sp = "$twin\Default\Secure Preferences"
node scripts\browser-integration\splice-prefs-key.mjs $sp oenpbjpibkeomkimhpldpmabdblmipoa 2>&1 | Out-File $report -Append

# Verify JSON still parses + entries
$obj = Get-Content $sp -Raw | ConvertFrom-Json
"after splice: bogfpdfo=$($null -ne $obj.extensions.settings.'bogfpdfoagkaebimmlcbgmfmanhbhhlm') oenpbjp=$($null -ne $obj.extensions.settings.'oenpbjpibkeomkimhpldpmabdblmipoa') boieha=$($null -ne $obj.extensions.settings.'boiehajcdnhbdmebpnbihmmdfafihlkl') count=$($obj.extensions.settings.PSObject.Properties.Name.Count)" | Out-File $report -Append

# Launch and test
$beforeLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$logFile = "$env:TEMP\chatterino-splice-startup.log"
Remove-Item $logFile -Force -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$twin", '--profile-directory=Default',
  '--no-first-run','--no-default-browser-check',
  '--enable-logging','--v=1', "--log-file=$logFile",
  'about:blank'
) -PassThru
Start-Sleep -Seconds 50
$afterLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$new1 = $afterLogs | Where-Object { $_ -notin $beforeLogs }
"HOST_LOGS_AFTER_SPLICE: $($new1 -join ', ')" | Out-File $report -Append
foreach ($nl in $new1) { Get-Content "$env:TEMP\$nl" | Select-Object -First 8 | Out-File $report -Append }
Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

# Post-launch prefs state: did Edge accept or wipe?
$sp2 = Get-Content $sp -Raw | ConvertFrom-Json
"post-launch count=$($sp2.extensions.settings.PSObject.Properties.Name.Count)" | Out-File $report -Append
"post-launch bogfpdfo=$($null -ne $sp2.extensions.settings.'bogfpdfoagkaebimmlcbgmfmanhbhhlm')" | Out-File $report -Append
"load errors:" | Out-File $report -Append
Select-String -Path $logFile -Pattern "couldn't load the extension" | ForEach-Object { $_.Line | Out-File $report -Append }
Get-Content $report
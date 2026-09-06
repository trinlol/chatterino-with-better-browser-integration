$ErrorActionPreference = 'Continue'

$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$userData = "$env:LOCALAPPDATA\Microsoft\Edge\User Data"
$realProfile = "$userData\Default"

$replica = Join-Path $env:TEMP ('chatterino-replica-full-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $replica | Out-Null

# Full clone excluding caches/sockets/locks
$excludeDirs = @('Cache','Code Cache','GPUCache','GrShaderCache','DawnGraphiteCache','DawnWebGPUCache',
  'ShaderCache','GraphiteDawnCache','Service Worker','ScriptCache','Crashpad','_platform_specific',
  'component_crx_cache','Shared Proto DB','optimization_guide_model_store','File System','blob_storage',
  'IndexedDB','Local Storage','Session Storage','Web Applications','extensions_crx_cache','Thumbnails',
  'Download Service','Media Foundation CDM Cache','DawnCache')
$excludeFiles = @('LOCK','lockfile','SingletonLock','SingletonCookie','SingletonSocket','First Run','DevToolsActivePort')
$argBase = @($realProfile, $replica, '/E', '/NFL', '/NDL', '/NJH', '/NJS', '/NC', '/NS', '/NP', '/R:0', '/W:0')
foreach ($d in $excludeDirs) { $argBase += @('/XD', $d) }
foreach ($f in $excludeFiles) { $argBase += @('/XF', $f) }
& robocopy @argBase | Out-Null
Write-Host "replica: $replica (robocopy exit $LASTEXITCODE)"

# Copy Local State from User Data root
Copy-Item "$userData\Local State" "$replica\Local State" -Force -ErrorAction SilentlyContinue

$beforeLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name

$p = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$replica",
  '--no-first-run','--no-default-browser-check',
  '--enable-logging','--v=1',
  'edge://extensions/'
) -PassThru
Write-Host "edge pid: $($p.Id)"
Start-Sleep -Seconds 16

$afterLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$newLogs = $afterLogs | Where-Object { $_ -notin $beforeLogs }
Write-Host "new chatterino native host logs: $($newLogs -join ', ')"

$extLog = Join-Path $replica 'chrome_debug.log'
Write-Host "chrome_debug.log exists: $(Test-Path $extLog)"
if (Test-Path $extLog) {
  $content = Get-Content $extLog
  $regs = $content | Select-String -Pattern 'extension id:' | ForEach-Object { $_.Line } |
    ForEach-Object { if ($_ -match 'extension id:\s+([a-p]{32})') { $matches[1] } } |
    Where-Object { $_ } | Sort-Object -Unique
  Write-Host "script contexts with extension ids: $($regs -join ', ')"
  $userIds = $regs | Where-Object { $_ -notin @('mhjfbmdgcfjbbpaeojofohoefgiehjai','iglcjdemknebjbklcgkfaebgojjphkec','dgiklkfkllikcanfonkcabmbdfmgleag','nkeimhogjdpnpccoofpliimaahmaaome','ndcpkimcihhghdcddljkfmmjccdmcmof','ihmafllikibpmigkcoadcmckbfhibefp','fikbjbembnmfhppjfnmfkahdhfohhjmg','ncbjelpjchkpbikbpkcchkhkblodoama','jdiccldimpdaibmpdkjnbmckianbfold') }
  Write-Host "NON-component extension ids: $($userIds -join ', ')"
}

Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue |
  Where-Object { $_.CommandLine -like "*$replica*" } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }

Write-Host "KEEP_REPLICA=$replica"
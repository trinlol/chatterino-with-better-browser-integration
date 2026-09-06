$ErrorActionPreference = 'Continue'
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$userData = "$env:LOCALAPPDATA\Microsoft\Edge\User Data"
$twin = Join-Path $env:TEMP ('chatterino-twin-' + [guid]::NewGuid().ToString('N').Substring(0,8))
$report = Join-Path $env:TEMP 'chatterino-twin-report.txt'
"TIME=$(Get-Date -Format o)" | Out-File $report

Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3

# 1) Full copy of real User Data (exclude only bulky GPU/shader caches and lock files)
New-Item -ItemType Directory -Path $twin | Out-Null
$excludeDirNames = @('Cache','Code Cache','GPUCache','GrShaderCache','DawnGraphiteCache','DawnWebGPUCache','ShaderCache','GraphiteDawnCache','Crashpad','component_crx_cache','extensions_crx_cache')
Get-ChildItem $userData | ForEach-Object {
  if ($_.PSIsContainer) {
    if ($excludeDirNames -contains $_.Name) { return }
    Copy-Item $_.FullName (Join-Path $twin $_.Name) -Recurse -Force -ErrorAction SilentlyContinue
  } else {
    if ($_.Name -in @('LOCK','lockfile')) { return }
    Copy-Item $_.FullName (Join-Path $twin $_.Name) -Force -ErrorAction SilentlyContinue
  }
}
"TWIN=$twin" | Out-File $report -Append
$twinSize = [Math]::Round(((Get-ChildItem $twin -Recurse -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum / 1MB), 0)
"TWIN_SIZE_MB=$twinSize" | Out-File $report -Append

# 2) Baseline launch of the twin - does the failure reproduce?
$beforeLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$p1 = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$twin", '--profile-directory=Default',
  '--no-first-run','--no-default-browser-check',
  'about:blank'
) -PassThru
Start-Sleep -Seconds 45
$afterLogs = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$new1 = $afterLogs | Where-Object { $_ -notin $beforeLogs }
"PHASE1_twin_baseline_host_logs: $($new1 -join ', ')" | Out-File $report -Append
Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3

# 3) Surgical ghost removal in the twin
$spPath = "$twin\Default\Secure Preferences"
$spRaw = Get-Content $spPath -Raw
Copy-Item $spPath "$spPath.pre-surgery.bak" -Force
$obj = $spRaw | ConvertFrom-Json
$removed = @()
foreach ($ghost in @('oenpbjpibkeomkimhpldpmabdblmipoa','boiehajcdnhbdmebpnbihmmdfafihlkl')) {
  if ($obj.extensions.settings.$ghost) {
    $obj.extensions.settings.PSObject.Properties.Remove($ghost)
    $removed += $ghost
  }
}
$obj | ConvertTo-Json -Depth 100 -Compress | Set-Content -Path $spPath -Encoding UTF8 -NoNewline
"PHASE2_removed_ghosts: $($removed -join ', ')" | Out-File $report -Append

# 4) Launch the surgically-edited twin - does auto-load return?
$before2 = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$p2 = Start-Process -FilePath $edge -ArgumentList @(
  "--user-data-dir=$twin", '--profile-directory=Default',
  '--no-first-run','--no-default-browser-check',
  'about:blank'
) -PassThru
Start-Sleep -Seconds 45
$after2 = Get-ChildItem "$env:TEMP\chatterino-native-host-*.log" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$new2 = $after2 | Where-Object { $_ -notin $before2 }
"PHASE3_after_surgery_host_logs: $($new2 -join ', ')" | Out-File $report -Append
foreach ($nl in $new2) {
  "--- $nl ---" | Out-File $report -Append
  Get-Content "$env:TEMP\$nl" -ErrorAction SilentlyContinue | Select-Object -First 10 | Out-File $report -Append
}

# Did the twin's prefs survive the edit (post-launch state)?
$spPost = Get-Content $spPath -Raw | ConvertFrom-Json
$ghostsAfter = $spPost.extensions.settings.PSObject.Properties | Where-Object { $_.Value.location -eq 4 -and $_.Name -ne 'bogfpdfoagkaebimmlcbgmfmanhbhhlm' } | ForEach-Object { $_.Name }
"PHASE3_ghosts_after_launch: $($ghostsAfter -join ', ')" | Out-File $report -Append
$entriesAfter = ($spPost.extensions.settings.PSObject.Properties | Measure-Object).Count
"PHASE3_extension_entry_count_after: $entriesAfter" | Out-File $report -Append
$e = $spPost.extensions.settings.'bogfpdfoagkaebimmlcbgmfmanhbhhlm'
"PHASE3_bogfpdfo_present: $($null -ne $e) last_update=$($e.last_update_time)" | Out-File $report -Append

Get-Process msedge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
"DONE" | Out-File $report -Append
Write-Output "TWIN=$twin"
Write-Output "REPORT=$report"
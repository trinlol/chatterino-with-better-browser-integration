[CmdletBinding()]
param(
    [switch]$Launch,
    [string]$EdgePath = $env:CHATTERINO_E2E_EDGE_PATH,
    [string]$ChatterinoPath = $env:CHATTERINO_E2E_CHATTERINO_PATH,
    [string]$ExtensionPath = ""
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($ExtensionPath)) {
    $ExtensionPath = Join-Path $PSScriptRoot "..\..\chatterino-extension"
}

function Write-ManualGate {
    param([string]$Message)
    [Console]::Error.WriteLine("Browser/native real-app gate: $Message")
    Write-Host "This command is intentionally not a pass-through mock. Run with -Launch and CHATTERINO_E2E_ALLOW_PROCESS_CONTROL=1 on a dedicated Windows test machine."
    exit 2
}

if (-not $Launch) {
    Write-ManualGate "launch is opt-in"
}
if ($env:CHATTERINO_E2E_ALLOW_PROCESS_CONTROL -ne "1") {
    Write-ManualGate "process control is not explicitly authorized"
}
if (-not $EdgePath -or -not (Test-Path -LiteralPath $EdgePath)) {
    Write-ManualGate "set CHATTERINO_E2E_EDGE_PATH to an Edge Stable executable"
}
if (-not $ChatterinoPath -or -not (Test-Path -LiteralPath $ChatterinoPath)) {
    Write-ManualGate "set CHATTERINO_E2E_CHATTERINO_PATH to the packaged Chatterino Better Browser executable"
}
if (-not (Test-Path -LiteralPath (Join-Path $ExtensionPath "manifest.json"))) {
    Write-ManualGate "extension path does not contain manifest.json"
}

$profile = Join-Path ([System.IO.Path]::GetTempPath()) ("chatterino-e2e-" + [guid]::NewGuid())
$edgeArgs = @(
    "--user-data-dir=$profile",
    "--load-extension=$ExtensionPath",
    "--remote-debugging-port=9222",
    "https://www.twitch.tv/"
)

Write-Host "Launching an isolated Edge profile and the supplied packaged Chatterino executable."
Write-Host "No registry key is changed. Only the two process IDs printed below may be stopped by the dedicated runner."
$edge = Start-Process -FilePath $EdgePath -ArgumentList $edgeArgs -PassThru
$desktop = Start-Process -FilePath $ChatterinoPath -PassThru

[pscustomobject]@{
    status = "manual-observation-required"
    edgePid = $edge.Id
    chatterinoPid = $desktop.Id
    profile = $profile
    required = @(
        "Edge Stable, Windows 11, 100% DPI, 100% zoom, two Twitch windows",
        "browser-first and desktop-first startup",
        "native host death, desktop restart, navigation, and worker recreation",
        "Twitch chat fallback under 2 seconds",
        "geometry within 2 physical pixels"
    )
} | ConvertTo-Json -Depth 4

Write-ManualGate "manual observations must be recorded with the release evidence; this script never claims them automatically"

param(
    [Parameter(Mandatory=$true)][string]$Version,
    [string]$PublicKey = (Join-Path $PSScriptRoot '..\crt\artifact-public.pem'),
    [string]$Key = (Join-Path $PSScriptRoot '..\crt\artifact-private.pem'),
    [ValidateSet('upgrade-release', 'upgrade-debug')][string]$Preset = 'upgrade-release',
    [string]$OutputDirectory,
    [string]$ArtifactTool = 'D:\Technology_stack\Web\mender\tools\mender-artifact',
    [string]$WslDistro = 'Ubuntu-24.04',
    [ValidateSet('urllib', 'chrome')][string]$HttpClient = 'urllib',
    [switch]$VerifyTLS,
    [string]$Server = 'https://127.0.0.1:18443',
    [string]$Credentials = 'D:\Technology_stack\Web\mender\admin-credentials.json',
    [string]$CA = 'D:\Technology_stack\Web\mender\server\compose\certs\mender-ca.crt',
    [switch]$Upload
)
$ErrorActionPreference = 'Stop'
$scriptArgs = @((Join-Path $PSScriptRoot 'release.py'), 'stm32', '--version', $Version,
    '--public-key', $PublicKey, '--key', $Key, '--preset', $Preset,
    '--artifact-tool', $ArtifactTool, '--wsl-distro', $WslDistro, '--http-client', $HttpClient,
    '--server', $Server, '--credentials', $Credentials, '--ca', $CA)
if ($OutputDirectory) { $scriptArgs += @('--output', $OutputDirectory) }
if ($Upload) { $scriptArgs += '--upload' }
if ($VerifyTLS) { $scriptArgs += '--verify-tls' }
& python @scriptArgs
if ($LASTEXITCODE -ne 0) { throw 'STM32 release failed; inspect output logs/receipt before retrying.' }

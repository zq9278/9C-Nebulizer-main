$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'run_electron_host.ps1')
exit $LASTEXITCODE

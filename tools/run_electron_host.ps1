$ErrorActionPreference = 'Stop'
$WorkbenchDirectory = Join-Path $PSScriptRoot 'workbench'
Push-Location -LiteralPath $WorkbenchDirectory
try {
    if (!(Test-Path -LiteralPath 'node_modules')) {
        & npm.cmd ci
        if ($LASTEXITCODE -ne 0) { throw 'npm dependency installation failed' }
    }
    & npm.cmd start
    if ($LASTEXITCODE -ne 0) { throw 'Electron workbench failed to start' }
} finally { Pop-Location }

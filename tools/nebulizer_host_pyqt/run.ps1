$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PythonCandidates = @()

if ($env:VIRTUAL_ENV) {
    $PythonCandidates += (Join-Path $env:VIRTUAL_ENV "Scripts\python.exe")
}

$PythonCandidates += @(
    "C:\Users\zq\.zinstaller\.venv\Scripts\python.exe",
    "D:\Technology_stack\Zephyr\zephyrproject_test\.venv\Scripts\python.exe",
    "D:\zephyrproject\.venv\Scripts\python.exe"
)

$DefaultPython = $PythonCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

if (-not $DefaultPython) {
    $PythonCmd = Get-Command python -ErrorAction SilentlyContinue
    if ($PythonCmd) {
        $DefaultPython = $PythonCmd.Source
    }
}

if (-not $DefaultPython) {
    Write-Error "No usable Python interpreter found. Activate a venv or install Python first."
}

Push-Location $ScriptDir
try {
    Write-Host "Using Python: $DefaultPython"

    $depsReady = $false
    try {
        & $DefaultPython -c "import PyQt6, serial" *> $null
        $depsReady = ($LASTEXITCODE -eq 0)
    }
    catch {
        $depsReady = $false
    }

    if (-not $depsReady) {
        Write-Host "Installing host UI dependencies..."
        & $DefaultPython -m pip install -r requirements.txt
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to install dependencies."
        }
    }

    & $DefaultPython app.py
}
finally {
    Pop-Location
}

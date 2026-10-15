$ErrorActionPreference = 'Stop'
$DebugHostDir = Join-Path $PSScriptRoot 'nebulizer_debug'
$DebugHostPython = if ($env:VIRTUAL_ENV) { Join-Path $env:VIRTUAL_ENV 'Scripts/python.exe' } else { (Get-Command python -ErrorAction Stop).Source }
& $DebugHostPython -c "import PyQt6, elftools, pylink, pyocd, serial"
if ($LASTEXITCODE -ne 0) { throw "Missing dependencies. Run: python -m pip install -r tools/nebulizer_debug/requirements.txt" }
& $DebugHostPython (Join-Path $DebugHostDir 'app.py')
exit $LASTEXITCODE

[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [ValidateSet('doctor', 'setup', 'test', 'firmware-build', 'sim-build', 'sim-test',
               'sim-update-goldens', 'flash', 'monitor', 'all')]
  [string]$Command = 'doctor',

  [ValidateSet('velxio', 'wokwi')]
  [string]$Backend = 'velxio',
  [switch]$FullSuite,
  [string]$Run,
  [string]$Scenario,
  [string]$Port
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$venvRoot = Join-Path $repoRoot '.tools\venv'
$venvPython = Join-Path $venvRoot 'Scripts\python.exe'

function Invoke-Bench([string[]]$BenchArgs) {
  & python (Join-Path $repoRoot 'tools/bench_cli.py') @BenchArgs
  if ($LASTEXITCODE -ne 0) { throw "Bench command failed with exit code $LASTEXITCODE." }
}
function Initialize-HostVenv {
  if (-not (Test-Path -LiteralPath $venvPython)) {
    & python -m venv $venvRoot
    if ($LASTEXITCODE -ne 0) { throw 'Unable to create .tools/venv.' }
  }
  & $venvPython -m pip install --disable-pip-version-check --quiet --upgrade `
      'pip==25.2' 'esptool==5.0.2' 'pyserial==3.5'
  if ($LASTEXITCODE -ne 0) { throw 'Unable to install host flashing tools.' }
}

if ($Command -eq 'flash') {
  if ([string]::IsNullOrWhiteSpace($Port)) { throw 'flash requires -Port COMx.' }
  Initialize-HostVenv
  Invoke-Bench @('firmware-build')
  & $venvPython (Join-Path $repoRoot 'tools/flash_firmware.py') --port $Port
  if ($LASTEXITCODE -ne 0) { throw 'Firmware flash failed.' }
} elseif ($Command -eq 'monitor') {
  if ([string]::IsNullOrWhiteSpace($Port)) { throw 'monitor requires -Port COMx.' }
  Initialize-HostVenv
  & $venvPython (Join-Path $repoRoot 'tools/serial_monitor.py') --port $Port
  if ($LASTEXITCODE -ne 0) { throw 'Serial monitor failed.' }
} else {
  $benchArgs = @($Command, '--backend', $Backend)
  if ($Scenario) { $benchArgs += @('--scenario', $Scenario) }
  if ($FullSuite) { $benchArgs += '--full-suite' }
  if ($Run) { $benchArgs += @('--run', $Run) }
  Invoke-Bench $benchArgs
  if ($Command -eq 'setup') { Initialize-HostVenv }
}

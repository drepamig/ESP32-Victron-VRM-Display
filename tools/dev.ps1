[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [ValidateSet('doctor', 'setup', 'test', 'firmware-build', 'sim-build', 'sim-test',
               'sim-update-goldens', 'flash', 'monitor', 'all')]
  [string]$Command = 'doctor',

  [string]$Scenario,
  [string]$Port
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$image = 'victron-cyd-virtual-bench:2026-09-03'
$venvRoot = Join-Path $repoRoot '.tools\venv'
$venvPython = Join-Path $venvRoot 'Scripts\python.exe'

function Invoke-Docker([string[]]$BenchArgs, [switch]$NeedsToken) {
  docker image inspect $image *> $null
  if ($LASTEXITCODE -ne 0) {
    throw "Bench image is missing. Run: tools/dev.ps1 setup"
  }

  $dockerArgs = @('run', '--rm', '--init',
                  '--volume', "${repoRoot}:/workspace",
                  '--workdir', '/workspace')
  if ($NeedsToken) {
    if ([string]::IsNullOrWhiteSpace($env:WOKWI_CLI_TOKEN)) {
      throw 'WOKWI_CLI_TOKEN is required for simulator execution.'
    }
    # Docker receives the value from the host environment; it is never placed
    # in this process command line or persisted in the image.
    $dockerArgs += @('--env', 'WOKWI_CLI_TOKEN')
  }
  $dockerArgs += @($image, 'bash', 'tools/bench.sh') + $BenchArgs
  & docker @dockerArgs
  if ($LASTEXITCODE -ne 0) {
    throw "Bench command failed with exit code $LASTEXITCODE."
  }
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

switch ($Command) {
  'setup' {
    & docker build --tag $image --file (Join-Path $repoRoot '.devcontainer\Dockerfile') $repoRoot
    if ($LASTEXITCODE -ne 0) { throw 'Docker image build failed.' }
    Initialize-HostVenv
    Invoke-Docker @('doctor')
  }
  'doctor' { Invoke-Docker @('doctor') }
  'test' { Invoke-Docker @('test') }
  'firmware-build' { Invoke-Docker @('firmware-build') }
  'sim-build' { Invoke-Docker @('sim-build') }
  'sim-test' {
    $args = @('sim-test')
    if ($Scenario) { $args += @('--scenario', $Scenario) }
    Invoke-Docker $args -NeedsToken
  }
  'sim-update-goldens' {
    $args = @('sim-update-goldens')
    if ($Scenario) { $args += @('--scenario', $Scenario) }
    Invoke-Docker $args -NeedsToken
  }
  'flash' {
    if ([string]::IsNullOrWhiteSpace($Port)) { throw 'flash requires -Port COMx.' }
    Initialize-HostVenv
    $imagePath = Join-Path $repoRoot 'build\firmware\VictronCYD_Modbus.ino.merged.bin'
    if (-not (Test-Path -LiteralPath $imagePath)) {
      Invoke-Docker @('firmware-build')
    }
    & $venvPython -m esptool --chip esp32 --port $Port write-flash 0x0 $imagePath
    if ($LASTEXITCODE -ne 0) { throw 'Firmware flash failed.' }
  }
  'monitor' {
    if ([string]::IsNullOrWhiteSpace($Port)) { throw 'monitor requires -Port COMx.' }
    Initialize-HostVenv
    & $venvPython (Join-Path $repoRoot 'tools\serial_monitor.py') --port $Port
    if ($LASTEXITCODE -ne 0) { throw 'Serial monitor failed.' }
  }
  'all' { Invoke-Docker @('all') -NeedsToken }
}

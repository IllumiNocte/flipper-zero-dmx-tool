$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$venv = Join-Path $projectRoot ".venv"
$python = Get-Command python -ErrorAction Stop

if(-not (Test-Path -LiteralPath (Join-Path $venv "Scripts\python.exe"))) {
    & $python.Source -m venv $venv
}

$venvPython = Join-Path $venv "Scripts\python.exe"
& $venvPython -m pip install --upgrade "ufbt==0.2.6"

$env:UFBT_HOME = Join-Path $projectRoot ".ufbt"
$sdkUrl = "https://github.com/DarkFlippers/unleashed-firmware/releases/download/unlshd-090/flipper-z-f7-sdk-unlshd-090.zip"
& (Join-Path $venv "Scripts\ufbt.exe") update --hw-target f7 --url $sdkUrl

Write-Host "uFBT is ready with the Unleashed 090 SDK (API 88.2)."
Write-Host "Build the app with .\build.ps1"


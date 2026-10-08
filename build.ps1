$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ufbt = Join-Path $projectRoot ".venv\Scripts\ufbt.exe"

if(-not (Test-Path -LiteralPath $ufbt)) {
    throw "uFBT is not installed. Run .\setup.ps1 first."
}

$env:UFBT_HOME = Join-Path $projectRoot ".ufbt"

Push-Location $projectRoot
try {
    & $ufbt
    if($LASTEXITCODE -ne 0) {
        throw "uFBT build failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}


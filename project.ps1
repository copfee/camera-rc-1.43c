$ErrorActionPreference = 'Stop'
if ($env:RC_PYTHON) {
    & $env:RC_PYTHON "$PSScriptRoot/tools/project.py" @args
} elseif (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 "$PSScriptRoot/tools/project.py" @args
} else {
    & python "$PSScriptRoot/tools/project.py" @args
}
exit $LASTEXITCODE


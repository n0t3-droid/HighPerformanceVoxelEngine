$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $projectRoot 'build'
$exePath = Join-Path $buildDir 'Release\HighPerformanceVoxelEngine.exe'

function Stop-HveProcess {
    Get-Process HighPerformanceVoxelEngine -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 400
}

Write-Host '[HVE] Safe Release Build gestartet...'

for ($attempt = 1; $attempt -le 3; $attempt++) {
    Write-Host "[HVE] Versuch $attempt/3"

    Stop-HveProcess

    if (Test-Path $exePath) {
        attrib -R $exePath 2>$null
    }

    & cmake --build $buildDir --config Release -j
    if ($LASTEXITCODE -eq 0) {
        Write-Host '[HVE] Build erfolgreich.'
        exit 0
    }

    Write-Warning "[HVE] Build fehlgeschlagen (ExitCode=$LASTEXITCODE). Neuer Versuch..."
    Start-Sleep -Milliseconds 600
}

throw '[HVE] Build nach 3 Versuchen fehlgeschlagen (möglicher File-Lock/LNK1104).'

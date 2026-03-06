$ErrorActionPreference = 'Stop'

# AETHERFORGE VEGA ASCENSION (5700G + Radeon iGPU)
# Goal: ultra-responsive input, stable frametime, all-day playability.

Get-ChildItem Env: | Where-Object { $_.Name -like 'HVE_*' } | ForEach-Object {
    Remove-Item ("Env:" + $_.Name) -ErrorAction SilentlyContinue
}

$env:HVE_START_MENU = '1'
$env:HVE_WORLD_FILE = 'hve_world_vega_ascension.hvew'

# Hardware-aware auto mode
$env:HVE_AUTO_HW_QUALITY = '1'
$env:HVE_AUTO_VEGA_ASCENSION = '1'
$env:HVE_QUALITY = '1'
$env:HVE_LOWEND_CONTROLLER = '1'
$env:HVE_LARGE_MODE = '1'
$env:HVE_LARGE_TARGET_FPS = '60'

# Practical draw/stream range for Vega iGPU stability
$env:HVE_VIEWDIST = '14'
$env:HVE_STREAM_MARGIN = '8'
$env:HVE_UPLOAD_BUDGET = '128'
$env:HVE_UPLOAD_PUMP_CAP = '256'
$env:HVE_STREAM_STEPS_MAX = '4'
$env:HVE_STREAM_STEPS_OVERLOAD_MAX = '2'
$env:HVE_STREAM_STEPS_WHILE_MOVING = '2'
$env:HVE_STREAM_INFLIGHT_SOFTCAP = '820'
$env:HVE_PRELOAD_RADIUS = '8'
$env:HVE_PRELOAD_MAX_SEC = '16'

# Fast input priority (hard requirement)
$env:HVE_FAST_INPUT_PRIORITY = '1'
$env:HVE_FAST_INPUT_TIMESLICE_FACTOR = '0.05'
$env:HVE_FAST_INPUT_HOLD_SEC = '0.55'
$env:HVE_FAST_INPUT_INTENT_THRESHOLD = '0.14'
$env:HVE_FAST_INPUT_LAG_WARN_MS = '18'
$env:HVE_FAST_INPUT_RESPONSE_MIN = '0.84'
$env:HVE_FAST_INPUT_MAX_STREAM_STEPS = '2'
$env:HVE_FAST_INPUT_STREAM_CLAMP = '14'
$env:HVE_FAST_INPUT_UPLOAD_CLAMP = '84'

# Spike and stutter guards
$env:HVE_SPIKE_GUARD = '1'
$env:HVE_STUTTER_ALERTS = '1'
$env:HVE_COVERAGE_ALARM = '1'
$env:HVE_COVERAGE_ALARM_HIGH = '26'
$env:HVE_COVERAGE_ALARM_LOW = '9'
$env:HVE_COVERAGE_ALARM_BOOST = '18'
$env:HVE_STARVATION_RESCUE_HOLD_SEC = '1.4'
$env:HVE_STARVATION_RESCUE_MIN_STREAM = '20'
$env:HVE_STARVATION_RESCUE_MIN_HEIGHT = '20'
$env:HVE_STARVATION_RESCUE_UPLOAD_BOOST = '160'
$env:HVE_STARVATION_EXTRA_PUMP_PASSES = '2'
$env:HVE_STARVATION_EXTRA_PUMP_BUDGET = '160'
$env:HVE_FARFIELD_SUPPRESS_HOLD_SEC = '1.25'

# Far-field visuals tuned to avoid distance flicker
$env:HVE_HORIZON_MODE = '1'
$env:HVE_HORIZON_TERRAIN = '1'
$env:HVE_HORIZON_LEVELS = '4'
$env:HVE_HORIZON_GRID_CELLS = '88'
$env:HVE_HORIZON_POINTS_PER_FRAME = '4096'
$env:HVE_HORIZON_UPDATE_MIN_SEC = '0.26'
$env:HVE_IMPOSTOR_RING = '0'
$env:HVE_FAR_CLIP = '12000'
$env:HVE_FOG_HIGHALT_RELIEF = '1'
$env:HVE_FOG_HIGHALT_START = '110'
$env:HVE_FOG_HIGHALT_END = '320'
$env:HVE_FOG_HIGHALT_MIN_SCALE = '0.72'

# Cache policy for smoother revisits
$env:HVE_DISABLE_UNLOAD = '1'
$env:HVE_CACHE_MARGIN = '480'

$candidateLocalRelease = Join-Path $PSScriptRoot 'build\Release\HighPerformanceVoxelEngine.exe'
$candidateRootRelease = Join-Path (Split-Path $PSScriptRoot -Parent) 'build\Release\HighPerformanceVoxelEngine.exe'
$candidateRootDebug = Join-Path (Split-Path $PSScriptRoot -Parent) 'build\Debug\HighPerformanceVoxelEngine.exe'
$exe = @($candidateLocalRelease, $candidateRootRelease, $candidateRootDebug) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    throw 'HighPerformanceVoxelEngine.exe not found (checked root Debug/Release and local Release).'
}
Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe -Parent)
Write-Host 'Started HighPerformanceVoxelEngine with VEGA ASCENSION profile.'

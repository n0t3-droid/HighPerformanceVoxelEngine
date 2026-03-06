$ErrorActionPreference = 'Stop'

# Biome Far-Field profile (EXTREME + STABLE):
# Pushes distant world illusion aggressively while enforcing anti-stutter safety.

Get-ChildItem Env: | Where-Object { $_.Name -like 'HVE_*' } | ForEach-Object {
    Remove-Item ("Env:" + $_.Name) -ErrorAction SilentlyContinue
}

$env:HVE_START_MENU = '1'
$env:HVE_WORLD_FILE = 'hve_world_biome_farfield.hvew'

# Keep biome generation active (disable all flat/superflat rewrites)
$env:HVE_FORCE_FLAT_WORLD = '0'
$env:HVE_SUPERFLAT_MODE = '0'
$env:HVE_SUPERFLAT_REWRITE_LOADED = '0'

# Far-field priority over near-field detail
$env:HVE_QUALITY = '2'
$env:HVE_LARGE_MODE = '1'
$env:HVE_AUTO_HW_QUALITY = '0'
$env:HVE_LOWEND_CONTROLLER = '1'
$env:HVE_VIEWDIST = '20'
$env:HVE_STREAM_MARGIN = '10'
$env:HVE_FAR_CLIP = '18000'

# Streaming spends less on near ring, more budget left for far-field continuity
$env:HVE_STREAM_DUAL_QUEUE = '1'
$env:HVE_STREAM_NEAR_RING = '4'
$env:HVE_STREAM_NEAR_SHARE = '62'
$env:HVE_UPLOAD_BALANCED_SPLIT = '1'
$env:HVE_UPLOAD_NEAR_SHARE = '64'
$env:HVE_UPLOAD_NEAR_RADIUS = '8'

# Keep interaction responsive while flying
$env:HVE_STREAM_LOOKAHEAD_SEC = '1.00'
$env:HVE_STREAM_LOOKAHEAD_MAX_CHUNKS = '16'
$env:HVE_STREAM_LOOKAHEAD_SPEED_MULT = '0.55'
$env:HVE_STREAM_FLIGHT_STEPS_BONUS = '2'
$env:HVE_STREAM_FLIGHT_UPLOAD_BOOST = '160'
$env:HVE_STREAM_INFLIGHT_SOFTCAP = '1100'
$env:HVE_UPLOAD_BUDGET = '224'
$env:HVE_UPLOAD_PUMP_CAP = '512'
$env:HVE_STREAM_STEPS_MAX = '5'
$env:HVE_STREAM_STEPS_OVERLOAD_MAX = '3'
$env:HVE_STREAM_STEPS_WHILE_MOVING = '3'
$env:HVE_MAX_FIXED_STEPS = '2'
$env:HVE_RECOVERY_FIXED_STEPS = '1'
$env:HVE_FIXED_STEPS_WHILE_MOVING = '1'
$env:HVE_STARVATION_RESCUE_HOLD_SEC = '1.8'
$env:HVE_STARVATION_RESCUE_MIN_STREAM = '24'
$env:HVE_STARVATION_RESCUE_MIN_HEIGHT = '24'
$env:HVE_STARVATION_RESCUE_UPLOAD_BOOST = '192'
$env:HVE_STARVATION_EXTRA_PUMP_PASSES = '2'
$env:HVE_STARVATION_EXTRA_PUMP_BUDGET = '192'
$env:HVE_CHUNK_CATCHUP_ENABLED = '1'
$env:HVE_CHUNK_CATCHUP_TARGET = '6400'
$env:HVE_CHUNK_CATCHUP_UPLOAD_BOOST = '256'
$env:HVE_CHUNK_CATCHUP_STREAM_BONUS = '24'
$env:HVE_CHUNK_CATCHUP_STEPS_BONUS = '2'
$env:HVE_CHUNK_CATCHUP_DEFICIT_SOFT = '4096'
$env:HVE_CHUNK_CATCHUP_STREAM_BURST_MAX = '6'
$env:HVE_CHUNK_CATCHUP_UPLOAD_BURST_MAX = '640'

# Startup preload moderate
$env:HVE_PRELOAD_RADIUS = '14'
$env:HVE_PRELOAD_MAX_SEC = '32'
$env:HVE_PRELOAD_BLOCKS_INPUT = '0'
$env:HVE_STARTUP_FORCE_PRELOAD = '0'
$env:HVE_LOADING_INPUT_BREAKOUT = '1'
$env:HVE_STARTUP_LOADING_OVERLAY = '1'
$env:HVE_STARTUP_BLOCK_MIN_CHUNKS = '1400'
$env:HVE_STARTUP_BLOCK_MAX_SEC = '35'
$env:HVE_STARTUP_BLOCK_STALL_SEC = '10'

# Far-field illusion layers ON
$env:HVE_HORIZON_MODE = '1'
$env:HVE_HORIZON_TERRAIN = '1'
$env:HVE_HORIZON_GRID_CELLS = '96'
$env:HVE_HORIZON_CELL_SIZE = '32'
$env:HVE_HORIZON_LEVELS = '5'
$env:HVE_HORIZON_LEVEL_SCALE = '2.0'
$env:HVE_HORIZON_Y_OFFSET = '-2.0'
$env:HVE_HORIZON_UPDATE_STEP = '24'
$env:HVE_HORIZON_UPDATE_MIN_SEC = '0.28'
$env:HVE_HORIZON_POINTS_PER_FRAME = '8192'
$env:HVE_HORIZON_ADAPTIVE = '1'

$env:HVE_IMPOSTOR_RING = '0'
$env:HVE_IMPOSTOR_SEGMENTS = '224'
$env:HVE_IMPOSTOR_BANDS = '6'
$env:HVE_IMPOSTOR_INNER_RADIUS = '2200'
$env:HVE_IMPOSTOR_OUTER_RADIUS = '56000'
$env:HVE_IMPOSTOR_BASE_HEIGHT = '-14'
$env:HVE_IMPOSTOR_HEIGHT_AMP = '128'
$env:HVE_IMPOSTOR_NOISE_SCALE = '0.0011'
$env:HVE_IMPOSTOR_ADAPTIVE = '1'
$env:HVE_IMPOSTOR_BLEND_NEAR_START = '5200'
$env:HVE_IMPOSTOR_BLEND_NEAR_END = '9000'
$env:HVE_IMPOSTOR_BLEND_FAR_START = '0.90'
$env:HVE_IMPOSTOR_BLEND_MIN_ALPHA = '0.002'
$env:HVE_IMPOSTOR_DYNAMIC_BLEND = '0'
$env:HVE_IMPOSTOR_DYNAMIC_NEAR_SCALE = '0.90'
$env:HVE_IMPOSTOR_DYNAMIC_NEAR_WIDTH = '2100'
$env:HVE_IMPOSTOR_DYNAMIC_MIN_START = '900'
$env:HVE_IMPOSTOR_QUEUE_PRESSURE_BLEND = '0'
$env:HVE_IMPOSTOR_QUEUE_PRESSURE_PUSH = '2200'
$env:HVE_IMPOSTOR_QUEUE_PRESSURE_LAG_MS = '34'
$env:HVE_IMPOSTOR_AUTOTUNE_PUSH = '0'
$env:HVE_IMPOSTOR_AUTOTUNE_TARGET = '0.34'
$env:HVE_IMPOSTOR_AUTOTUNE_INTERVAL_SEC = '1.2'
$env:HVE_IMPOSTOR_AUTOTUNE_MIN_SAMPLES = '4'
$env:HVE_IMPOSTOR_AUTOTUNE_GAIN = '0.85'
$env:HVE_IMPOSTOR_AUTOTUNE_MUL_MIN = '0.60'
$env:HVE_IMPOSTOR_AUTOTUNE_MUL_MAX = '1.80'

# Do not keep huge close cache; prioritize smooth far continuity
$env:HVE_DISABLE_UNLOAD = '1'
$env:HVE_CACHE_MARGIN = '640'

# Stability guards (keep controls and frametime stable under far-field pressure)
$env:HVE_SPIKE_GUARD = '1'
$env:HVE_STUTTER_ALERTS = '1'
$env:HVE_COVERAGE_ALARM = '1'
$env:HVE_COVERAGE_ALARM_HIGH = '30'
$env:HVE_COVERAGE_ALARM_LOW = '10'
$env:HVE_COVERAGE_ALARM_BOOST = '20'
$env:HVE_CONTROL_OBS = '1'
$env:HVE_CONTROL_MITIGATION = '1'
$env:HVE_CONTROL_RESPONSE_THRESHOLD = '0.64'
$env:HVE_CONTROL_LAG_WARN_MS = '30'
$env:HVE_FAST_INPUT_PRIORITY = '1'
$env:HVE_FAST_INPUT_TIMESLICE_FACTOR = '0.06'
$env:HVE_FAST_INPUT_HOLD_SEC = '0.50'
$env:HVE_FAST_INPUT_INTENT_THRESHOLD = '0.16'
$env:HVE_FAST_INPUT_LAG_WARN_MS = '20'
$env:HVE_FAST_INPUT_RESPONSE_MIN = '0.80'
$env:HVE_FAST_INPUT_MAX_STREAM_STEPS = '2'
$env:HVE_FAST_INPUT_STREAM_CLAMP = '16'
$env:HVE_FAST_INPUT_UPLOAD_CLAMP = '88'
$env:HVE_FARFIELD_SUPPRESS_HOLD_SEC = '1.30'

$candidateLocalRelease = Join-Path $PSScriptRoot 'build\Release\HighPerformanceVoxelEngine.exe'
$candidateRootRelease = Join-Path (Split-Path $PSScriptRoot -Parent) 'build\Release\HighPerformanceVoxelEngine.exe'
$candidateRootDebug = Join-Path (Split-Path $PSScriptRoot -Parent) 'build\Debug\HighPerformanceVoxelEngine.exe'
$exe = @($candidateLocalRelease, $candidateRootRelease, $candidateRootDebug) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    throw 'HighPerformanceVoxelEngine.exe not found (checked root Debug/Release and local Release).'
}
Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe -Parent)
Write-Host 'Started HighPerformanceVoxelEngine with BIOME FAR-FIELD profile.'

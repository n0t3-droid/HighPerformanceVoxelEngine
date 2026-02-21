# Walk + Spawn Profile Presets

## Goal
Fast switching between two tested runtime presets after the movement/collision/spawn fixes in `Application.cpp`.

## Usage
Run from PowerShell in `HighPerformanceVoxelEngine`:

- Conservative: `./run_profile_conservative.ps1`
- Balanced: `./run_profile_balanced.ps1`
- Aggressive: `./run_profile_aggressive.ps1`
- Minecraft-Emulation: `./run_profile_minecraft.ps1`

If script execution is blocked once, use:

`Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass`

## Profiles

### Conservative
- Prioritizes terrain safety and stable step-up behavior.
- Wider spawn search and stricter flat-area checks.
- Slightly lower view/upload budgets to reduce stress during heavy terrain transitions.

### Aggressive
- Prioritizes render distance and throughput.
- Keeps new collision protections, but uses lighter spawn strictness and higher streaming budgets.

### Balanced
- Middle-ground profile for daily play/testing.
- Keeps collision and spawn safeguards close to conservative mode while maintaining near-aggressive streaming distance.

### Minecraft-Emulation
- Focused on natural block traversal when climbing and descending terrain lips/edges.
- Uses collision skin + stronger step-down assist + forgiving jump timing.

## Important Variables
- `HVE_WALK_COLLISION_RADIUS`: Horizontal body collision radius.
- `HVE_WALK_COLLISION_SKIN`: Inset for horizontal collision probes to avoid edge snagging.
- `HVE_SPAWN_SEARCH_RADIUS`: How far safe-spawn search can scan.
- `HVE_SPAWN_FLAT_RADIUS`: Neighbor radius used to validate spawn flatness.
- `HVE_SPAWN_FLAT_MAX_DELTA`: Max allowed height delta per neighbor around spawn.

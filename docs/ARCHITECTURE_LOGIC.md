# Architecture & Logic Deep Dive

This document outlines the specific logic required to achieve the performance seen in the analyzed videos.

## 1. The Rendering Logic (Vercidium's Method)

Standard Minecraft clones draw chunks one by one. This is slow because the CPU has to talk to the GPU thousands of times per frame.

**The Optimized Logic:**

- **The Super Buffer:** Allocate one massive VertexBuffer (e.g., 500MB) on the GPU at startup.
- **Chunk Allocation:** When a chunk is meshed, assign it a slice of this Super Buffer.
- **Command Generation:** Instead of calling draw(), write a struct to a buffer:

```cpp
struct DrawCommand {
        uint count;         // Number of indices
        uint instanceCount; // 1
        uint firstIndex;    // Offset in index buffer
        uint baseVertex;    // Offset in vertex buffer
        uint baseInstance;  // 0
};
```

- **GPU Execution:** Call `glMultiDrawElementsIndirect`. The GPU reads the list of commands and executes them all instantly.

## 2. Terrain Generation Logic (IGoByLotsOfNames' Method)

We do not want boring hills. We want domain-warped terrain.

**Logic Flow:**

- **Input:** (x, z) global coordinates.
- **Warping:**

    ```cpp
    float warpX = Noise(x * 0.01, z * 0.01);
    float warpZ = Noise(z * 0.01 + 50, x * 0.01 + 50);
    // Add warp to original coords
    float finalHeight = Noise(x + warpX * 40, z + warpZ * 40);
    ```

- **Biome Selection:**
    Use a separate noise map for humidity and temperature.
    `if (Height > 100) -> Snow.`
    `if (Humidity < 0.2) -> Desert.`
- **Tree Placement:**
    Do not place trees during chunk generation (cross-chunk issue).
    Place trees in a second pass called decoration.

## 3. Vertex Data Packing (Memory Optimization)

To reduce memory bandwidth, compress vertex data.

**The Struct:**

```cpp
struct PackedVertex {
        uint32_t data;
};
```

**Bit Layout (32 bits total):**

- **Bits 0-4 (5 bits):** X Position (0-31)
- **Bits 5-9 (5 bits):** Y Position (0-31)
- **Bits 10-14 (5 bits):** Z Position (0-31)
- **Bits 15-17 (3 bits):** Normal Index (0=Up, 1=Down, 2=Left, etc.)
- **Bits 18-22 (5 bits):** U Texture Coord
- **Bits 23-27 (5 bits):** V Texture Coord
- **Bits 28-31 (4 bits):** Lighting Level (AO)

**Shader Unpacking:**
In the vertex shader, use bitwise operations (`&`, `>>`) to extract these values and convert them back to floats.

## 4. Greedy Meshing Logic

To reduce triangle count from millions to thousands.

**Algorithm:**

1. Select a face direction (e.g., UP).
2. Iterate through the chunk layer by layer.
3. If `Block[x][z]` is same as `Block[x+1][z]`, extend width.
4. If `Block[x][z]` row matches `Block[x][z+1]` row, extend height.
5. Generate one quad for this entire area instead of individual block faces.

**Note:** Requires texture tiling (UVs must be adjusted to repeat the texture).

## 5. Infinite World Logic

- **Coordinate System:** Use `glm::ivec3` for chunk coordinates.
- **Hashing:** Implement a custom hash for `glm::ivec3` to use it as a key in `std::unordered_map`.
- **Floating Origin (optional):** If the player goes too far (e.g., x > 10,000), shift the entire world back to (0,0,0) to prevent floating-point errors (jittery movement).

## 6. Report Integration Update (2026-02-21)

Based on the internal research report (`Advanced cinematic voxel sandbox engine research and implementation report.pdf`), the following engine-level updates are now aligned and active:

- **Deterministic, staged world pipeline direction** remains the target architecture:
    Macro maps (`height / climate / hydrology`) before voxel materialization.
    Hydrology pre-pass (Priority-Flood + flow routing) as the next worldgen milestone.
- **Build-system hotpath optimization (implemented):**
    Bulk block edits now collect center chunks first and expand to the 27-neighborhood once at bulk flush.
    Redundant chunk-existence checks in neighborhood remesh scheduling were removed.
    Remesh queue limits are cached (static config read once), avoiding repeated environment parsing in hot loops.
    Result: lower CPU overhead while placing/breaking blocks continuously.
- **Hydrology subsystem (implemented):**
    New module: `src/Game/World/Hydrology.h` + `src/Game/World/Hydrology.cpp`.
    Includes Priority-Flood depression filling, D8 flow-direction calculation, and flow accumulation map generation.
    Integrated into `Generation::SampleTerrainMetrics()` to produce flow-aware terrain quality signals.
- **Runtime build diagnostics defaults (implemented):**
    `HVE_BUILD_WATCH` defaults to off for normal gameplay performance.
    Can still be enabled for debugging when needed.
- **Wireframe stability path (implemented):**
    Dedicated wireframe caps for stream/upload/height/cull so debug view remains usable without collapsing FPS.

### Next high-impact implementation steps

1. Add a dedicated hydrology pass module (`Priority-Flood` + `D8` accumulation) in worldgen.
2. Add macro-map cache per region tile (height/climate/sediment) to stabilize cinematic terrain consistency.
3. Keep simulation authoritative and rendering-consumer only (no gameplay dependency on render meshes).

## 7. Low-End GPU Budget Update (2026-02-21)

Based on `Cinematic voxel sandbox on a low-end GPU budget.pdf`, the engine now applies a stricter frame-time-first strategy for iGPU/UMA systems:

- **Frame-time quality ladder (implemented):**
  New runtime controller (`HVE_LOWEND_CONTROLLER`) tracks fast and slow frame-time EMA.
  If frame pressure rises above budget (`HVE_LOWEND_TARGET_MS` + hysteresis), quality level ramps down quickly.
  If frame pressure stays under budget, quality level recovers slowly.
- **Priority order for adaptation (implemented):**
  First reduce upload budget per frame.
  Then reduce stream/view distance pressure.
  Keep unload radius consistent with reduced stream radius.
- **Live observability (implemented):**
  Window title now surfaces low-end controller state (`LQ level@ms`) for immediate diagnosis while playing.
- **Profile defaults (implemented):**
  `run_profile_balanced.ps1` and `run_profile_conservative.ps1` now enable low-end controller defaults tuned for stable ~45 FPS class budgets.

This keeps visual ambition while preserving stable frame-time on bandwidth-limited integrated GPUs.

# Architecture & Logic Deep Dive

This document outlines the specific logic required to achieve the performance seen in the analyzed videos.

## 1. The Rendering Logic (Vercidium's Method)

Standard Minecraft clones draw chunks one by one. This is slow because the CPU has to talk to the GPU thousands of times per frame.

**The Optimized Logic:**

*   **The Super Buffer:** Allocate one massive VertexBuffer (e.g., 500MB) on the GPU at startup.
*   **Chunk Allocation:** When a chunk is meshed, assign it a slice of this Super Buffer.
*   **Command Generation:** Instead of calling draw(), we write a struct to a buffer:

```cpp
struct DrawCommand {
    uint count;         // Number of indices
    uint instanceCount; // 1
    uint firstIndex;    // Offset in index buffer
    uint baseVertex;    // Offset in vertex buffer
    uint baseInstance;  // 0
};
```

*   **GPU Execution:** Call `glMultiDrawElementsIndirect`. The GPU reads the list of commands and executes them all instantly.

## 2. Terrain Generation Logic (IGoByLotsOfNames' Method)

We do not want boring hills. We want "Domain Warped" terrain.

**Logic Flow:**

*   **Input:** (x, z) global coordinates.
*   **Warping:**
    ```cpp
    float warpX = Noise(x * 0.01, z * 0.01);
    float warpZ = Noise(z * 0.01 + 50, x * 0.01 + 50);
    // Add warp to original coords
    float finalHeight = Noise(x + warpX * 40, z + warpZ * 40);
    ```
*   **Biome Selection:**
    *   Use a separate noise map for "Humidity" and "Temperature".
    *   `if (Height > 100) -> Snow.`
    *   `if (Humidity < 0.2) -> Desert.`
*   **Tree Placement:**
    *   Do not place trees during chunk generation (cross-chunk issue).
    *   Place trees in a second pass called "Decoration".

## 3. Vertex Data Packing (Memory Optimization)

To reduce memory bandwidth, we compress vertex data.

**The Struct:**
```cpp
struct PackedVertex {
    uint32_t data;
};
```

**Bit Layout (32 bits total):**
*   **Bits 0-4 (5 bits):** X Position (0-31)
*   **Bits 5-9 (5 bits):** Y Position (0-31)
*   **Bits 10-14 (5 bits):** Z Position (0-31)
*   **Bits 15-17 (3 bits):** Normal Index (0=Up, 1=Down, 2=Left, etc.)
*   **Bits 18-22 (5 bits):** U Texture Coord
*   **Bits 23-27 (5 bits):** V Texture Coord
*   **Bits 28-31 (4 bits):** Lighting Level (AO)

**Shader Unpacking:**
In the Vertex Shader, use bitwise operations (&, >>) to extract these values and convert them back to floats.

## 4. Greedy Meshing Logic

To reduce triangle count from millions to thousands.

**Algorithm:**
1.  Select a Face Direction (e.g., UP).
2.  Iterate through the chunk layer by layer.
3.  If `Block[x][z]` is same as `Block[x+1][z]`, extend width.
4.  If `Block[x][z]` row matches `Block[x][z+1]` row, extend height.
5.  Generate one quad for this entire area instead of individual block faces.

**Note:** Requires texture tiling (UVs must be adjusted to repeat the texture).

## 5. Infinite World Logic

*   **Coordinate System:** Use `glm::ivec3` for Chunk Coordinates.
*   **Hashing:** Implement a custom hash for `glm::ivec3` to use it as a key in `std::unordered_map`.
*   **Floating Origin:** (Optional) If the player goes too far (e.g., x > 10,000), shift the entire world back to (0,0,0) to prevent floating-point errors (jittery movement).

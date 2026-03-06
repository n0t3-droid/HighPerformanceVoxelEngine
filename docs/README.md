# High-Performance Engine (C++ / OpenGL)

A high-performance voxel engine research project based on techniques by Ylber Thaqi.

## 🛑 CRITICAL CODING RULES (Do Not Violate)

1. **NO Object-Oriented Blocks:**
   * **Bad:** `class DirtBlock : public Block { ... }` (Causes cache misses and memory overhead).
   * **Good:** `uint8_t chunkData[32*32*32];` where the ID determines properties via a lookup table.

2. **Chunk Architecture:**
   * **Chunk Size:** 32x32x32 (Fits nicely into CPU cache).
   * **Face Culling:** Never add a face to the mesh if it touches an opaque block.
   * **Face Separation:** Split the mesh into 6 separate index buffers (Up, Down, North, South, East, West) to stop rendering faces looking away from the camera.

3. **Rendering Pipeline (The "12,000 FPS" Method):**
   * **Do NOT use:** `glDrawArrays` per chunk.
   * **MUST USE:** `glMultiDrawElementsIndirect` (MDDI).
   * Pack all chunk meshes into one massive GPU Buffer. Use an `IndirectBuffer` to tell the GPU which chunks to draw.

4. **Vertex Compression (Bit Packing):**
   * Do not use `float` for block positions.
   * **Pack data into a single `uint32_t` integer:**
     * x, y, z (5-6 bits each, relative to chunk)
     * TextureID (8 bits)
     * Normal (3 bits)
   * See `docs/ARCHITECTURE_LOGIC.md` for bit layout.

5. **Multithreading:**
   * Chunk generation (Noise) and Mesh building **MUST** happen on a worker thread.
   * The **Main Thread** only handles Input, Window, and `glBufferSubData` (uploading results).
   * Use `std::async` or a custom `ThreadPool`.

## 📁 File Manifest & Responsibilities

### `src/core`

* **`Application.cpp`**: Main loop, initializes Window and Renderer.
* **`Input.cpp`**: Handles keyboard/mouse. Implements "Raw Input" for smooth camera looking.

### `src/world`

* **`Chunk.h`**: Stores the `uint8_t` array. Contains `bool isDirty` flag.
* **`ChunkManager.cpp`**: Stores a `std::unordered_map` of chunks. Handles loading/unloading chunks based on player distance.
* **`Generation.cpp`**: Implements FastNoiseLite or Perlin Noise.
  * **Logic:** Use "Domain Warping" (warp the coordinate input of noise with another noise) for realistic terrain.

### `src/renderer`

* **`Mesh.cpp`**: Handles the vertex buffer construction.
* **`Renderer.cpp`**: Sets up the OpenGL Context, VAOs, SSBOs (Shader Storage Buffer Objects).
* **`IndirectBuffer.cpp`**: Manages the command buffer for the GPU.

### `src/physics`

* **`PhysicsSystem.cpp`**: Implements AABB (Axis-Aligned Bounding Box) collision.
  * **Logic:** Discrete collision detection. Check player_pos + velocity against local blocks.

## 🛠 Setup Instructions

1. **Install Visual Studio 2022** (C++ Desktop Development).
2. **Install CMake**.
3. **Dependencies (vendor/):**
   * **GLFW** (Windowing)
   * **GLAD** (OpenGL Loader)
   * **GLM** (Math Library)
   * **stb_image** (Texture Loading)

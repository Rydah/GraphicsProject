# CS2 Volumetric Smoke — OpenGL 4.3 C++

Real-time volumetric smoke grenade simulation ported from Garrett Gunnell's Unity recreation to standalone OpenGL 4.3 / C++.

Reference: [Acerola — CS2 Smoke Grenades](https://www.youtube.com/watch?v=ryB8hT5TMSg)
Unity source: [GarrettGunnell/CS2-Smoke-Grenades](https://github.com/GarrettGunnell/CS2-Smoke-Grenades)

---

## Controls

| Input | Action |
|-------|--------|
| Left-drag | Orbit camera |
| Shift + left-drag | Pan camera target |
| Scroll wheel | Zoom in/out |
| Space | Detonate smoke grenade |
| N | Toggle Worley noise slice view |
| Up / Down | Move noise slice depth |
| ESC | Quit |

---

## Build

Open `Graphics Project.sln` in **Visual Studio 2022** and build (`Ctrl+Shift+B`).
Config: **Debug\|x64** or **Release\|x64**.

MSBuild CLI:
```
msbuild "Graphics Project.sln" /p:Configuration=Release /p:Platform=x64
```

---

## Source File Map (`src/`)

### Application Layer

| File | Purpose |
|------|---------|
| `main.cpp` | Entry point. Window init, GLFW callbacks, render loop. ~130 lines. |
| `GLDebug.h` | `enableGLDebug()` — registers OpenGL 4.3 debug message callback. `printGPUInfo()` — prints GPU/compute limits at startup. |
| `OrbitCamera.h` | `OrbitCamera` struct. Holds yaw/pitch/dist/target. Methods: `position()`, `view()`, `proj()`, `onMouseButton()`, `onMouseMove()`, `onScroll()`. Passed directly to GLFW callbacks. |
| `SelfTests.h` | `SelfTests::runAllTests()` — two GPU correctness tests run at startup: (1) compute shader writes squares into SSBO, CPU reads back; (2) `imageStore` round-trip on a 64³ R16F texture. |
| `NoiseDebugView.h` | `NoiseDebugView` struct. Owns the noise-slice visualization shader. Call `init()` once, `draw(noiseTex, quad)` each frame when `enabled == true`. |

### OpenGL Wrappers (`src/`)

| File | Class | Purpose |
|------|-------|---------|
| `shader.h` | `shader` | Compiles + links a vertex/fragment shader pair. Uniform setters: `setInt`, `setFloat`, `setVec3`, `setVec4`, `setMat4`, `setIVec3`. |
| `ComputeShader.h` | `ComputeShader` | Compiles + links a compute shader. `dispatch(x,y,z)` auto-divides by local group size. Same uniform setters as `shader`. |
| `Buffer.h` | `SSBOBuffer` | GPU array buffer (`GL_SHADER_STORAGE_BUFFER`). `allocate(bytes)`, `bindBase(point)`, `upload<T>()`, `download<T>()`, `clear()`. |
| `Texture3D.h` | `Texture3D` | Immutable 3D texture (`glTexStorage3D`). `bindImage(unit, access)` for compute `imageStore/imageLoad`. `bindSampler(unit)` for `texture()` lookups. |
| `Texture2D.h` | `Texture2D` | Same pattern as `Texture3D` for 2D textures. Used for FBO attachments and post-process outputs. |
| `Framebuffer.h` | `Framebuffer` | Off-screen render target. `attachColor()`, `attachDepth()`, `setDepthOnly()`, `isComplete()`. |

### Smoke Subsystems (`src/`)

| File | Class | Purpose |
|------|-------|---------|
| `Voxelizer.h` | `Voxelizer` | Parses OBJ manually, builds triangle SSBO, runs 13-axis SAT compute shader to fill `staticVoxels` SSBO. Also has `generateTestScene()` for a procedural room. |
| `FloodFill.h` | `VoxelFloodFill` | Ping-pong SSBO flood fill. `seed(worldPos)` plants the grenade. `propagate(steps, ...)` each frame grows the smoke. Ellipsoid shape via anisotropic decay. |
| `WorleyNoise.h` | `WorleyNoise` | Generates a 128³ `GL_R16F` Worley noise volume each frame. Hugo Elias hash, tiled cells, fBm octaves, domain warp, time animation. |
| `VoxelDebug.h` | `VoxelDebug` | Draws the voxel grid as instanced cubes. `draw()` for walls only. `drawWithSmoke()` for walls (blue) + smoke (orange→white by density). |
| `FullscreenQuad.h` | `FullscreenQuad` | 4-vertex clip-space quad (`GL_TRIANGLE_STRIP`). Used for full-screen shader passes. |
| `shaderSource.h` | — | Embedded GLSL 4.30 vertex + fragment shader strings for basic Phong scene rendering (legacy, kept for reference). |

---

## Data Flow (per frame)

```
[CPU] floodFill.propagate()
        -> re-seeds growing value at seed voxel
        -> dispatches flood fill compute shader (ping-pong SSBOs)
        -> ellipsoid constraint blocks voxels outside shape
        -> walls block propagation; smoke must travel around them

[CPU] worleyNoise.generate(time)
        -> compute shader writes animated Worley fBm -> Texture3D

[CPU] voxelDebug.drawWithSmoke(walls, smoke, view, proj, ...)
        -> instanced cube draw: blue = wall, orange/white = smoke density
```

---

## SSBO Binding Convention

| Binding | Contents | Written by | Read by |
|---------|----------|-----------|--------|
| 0 | Wall voxels `int[]` | Voxelizer (once) | FloodFill, VoxelDebug |
| 1 | Smoke ping buffer `int[]` | FloodFill (src) | FloodFill |
| 2 | Smoke pong buffer `int[]` | FloodFill (dst) | FloodFill |

---

## Key Algorithms

### Flood Fill (FloodFill.h)
Each voxel per step: `value = max(self, max(6_neighbors) - 1)`.
Seed value grows 1→64 over 4 s with ease-in (`2t²` / sigmoid blend).
Y-decay = 2, XZ-decay = 1 → ellipsoid wider than tall.
Ellipsoid gate in shader kills voxels outside `(dx/rx)²+(dy/ry)²+(dz/rx)² > 1`.

### Worley Noise (WorleyNoise.h)
`noise = (1 - minDist)^6` per cell, tiled with modulo wrapping.
3 fBm octaves with domain warp animated by `time * speed`.

### Voxelizer SAT (Voxelizer.h)
13-axis Separating Axis Theorem per triangle-AABB pair.
`atomicOr(voxels[idx], 1)` — safe for parallel triangle dispatch.

---

## Adding a New Subsystem

1. Create `src/MyFeature.h` with a struct + `init()`, `update()`, `destroy()`.
2. `#include "MyFeature.h"` in `main.cpp`.
3. Instantiate, call `init()` before the loop, `update()` inside, `destroy()` after.
4. Document its SSBO binding point in the table above if it uses one.

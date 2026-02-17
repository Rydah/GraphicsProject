# CS2 Volumetric Smoke — OpenGL C++ Implementation TODO

## Context

This is a port of Garrett Gunnell's (Acerola) Unity-based CS2 smoke grenade recreation into
**OpenGL 4.3+ / C++ from scratch**. The reference Unity project lives in this same repository.
Reference video: https://www.youtube.com/watch?v=ryB8hT5TMSg

The Unity project already implements the full system. This TODO is for building an equivalent
standalone C++ application using OpenGL compute shaders, SSBOs, and FBOs — no Unity, no engine.

### What the system does (top-level)
1. Voxelizes static scene geometry so smoke knows where walls are
2. Flood-fills smoke outward from a grenade detonation point, blocked by walls
3. Animates the smoke with tiled 3D Worley noise
4. Renders it volumetrically with a GPU ray marcher (Beer-Lambert, phase functions)
5. Deforms smoke when bullets pass through (SDF capsule holes)
6. Composites the result over the scene with Catmull-Rom upsampling + sharpening

---

## Unity → OpenGL Translation Map

| Unity Concept | OpenGL Equivalent |
|---|---|
| `ComputeBuffer` (int/struct array) | SSBO (`GL_SHADER_STORAGE_BUFFER`, `std430`) |
| Compute Shader (HLSL `.compute`) | Compute Shader (GLSL 4.30, `.comp`) |
| `RenderTexture` 3D | `GL_TEXTURE_3D` with `glTexStorage3D` + `imageStore`/`imageLoad` |
| `RenderTexture` 2D | `GL_TEXTURE_2D` attached to an FBO |
| `Graphics.Blit(src, dst, mat, pass)` | Full-screen quad drawn into FBO with bound shader |
| `OnRenderImage` post-process hook | Custom render loop, `glBindFramebuffer` per pass |
| Unity camera matrices | `glm::inverse(glm::perspective(...))`, `glm::inverse(glm::lookAt(...))` |
| `DispatchCompute(gx, gy, gz)` | `glDispatchCompute(gx, gy, gz)` |
| GPU Instancing (`DrawMeshInstancedIndirect`) | `glDrawArraysInstanced` |
| `ComputeBuffer.SetData` | `glBufferSubData` on the SSBO |
| `ComputeBuffer.GetData` | `glGetBufferSubData` on the SSBO |

---

## Required Libraries

| Library | Purpose | How to get |
|---|---|---|
| **GLFW 3.x** | Window creation + input | `vcpkg install glfw3` or cmake FetchContent |
| **GLAD** | OpenGL 4.3 Core function loader | Generate at https://glad.dav1d.de (GL 4.3, Core) — adds a single `glad.c` file |
| **GLM** | Math (vec3, mat4, glm::inverse, etc.) | `vcpkg install glm` |
| **Assimp** | Mesh loading for voxelizer | `vcpkg install assimp` |
| **CMake 3.20+** | Build system | cmake.org |

---

## Project File Structure

```
cs2-smoke-opengl/
├── CMakeLists.txt
├── assets/
│   └── meshes/
│       └── test_box.obj          ← simple room/box for testing walls
├── external/
│   └── glad/
│       ├── include/glad/glad.h
│       └── src/glad.c            ← compile as C, not C++
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── Window.h / .cpp       ← GLFW window + GLAD init
│   │   ├── Camera.h / .cpp       ← view/proj/inv matrices
│   │   └── Timer.h               ← deltaTime helper
│   ├── gl/
│   │   ├── Shader.h / .cpp       ← vert+frag shader RAII
│   │   ├── ComputeShader.h / .cpp← compute shader RAII + dispatch helpers
│   │   ├── Buffer.h / .cpp       ← SSBO wrapper
│   │   ├── Texture2D.h / .cpp    ← 2D texture + FBO attachment
│   │   ├── Texture3D.h / .cpp    ← 3D texture for volumes
│   │   └── Framebuffer.h / .cpp  ← FBO wrapper
│   ├── smoke/
│   │   ├── Voxelizer.h / .cpp    ← Triangle-AABB SAT voxelization
│   │   ├── FloodFill.h / .cpp    ← Ping-pong flood fill propagation
│   │   ├── WorleyNoise.h / .cpp  ← 128³ animated noise volume
│   │   ├── Raymarcher.h / .cpp   ← Volume ray march dispatcher
│   │   ├── BulletHoleManager.h / .cpp ← SDF capsule holes
│   │   └── SmokeSystem.h / .cpp  ← Orchestrates all subsystems
│   └── post/
│       ├── Upsampler.h / .cpp    ← Catmull-Rom bicubic upsample
│       └── Compositor.h / .cpp   ← Scene blend + unsharp mask
└── shaders/
    ├── fullscreen.vert            ← shared full-screen quad vertex shader
    ├── voxelize.comp              ← Triangle-AABB SAT (one thread per triangle)
    ├── flood_fill.comp            ← 6-neighbor propagation + bullet hole SDF
    ├── worley_noise.comp          ← tiled Worley + fBm → 3D texture
    ├── raymarch.comp              ← full volumetric raymarcher
    ├── upsample.frag              ← Catmull-Rom bicubic reconstruction
    └── composite.frag             ← Beer-Lambert blend + Laplacian sharpen
```

---

## Why OpenGL Wrappers? — Architecture Explanation

Raw OpenGL is a **C state machine** — you call global functions like `glBindTexture`, `glBufferData`, `glDispatchCompute`, etc. with integer IDs, and it's easy to forget a bind, mismatch a format, or leak a resource. The wrappers exist to turn each OpenGL concept into a self-contained C++ object that manages its own state.

Here's what each wrapper does and **where it gets used in the smoke system**:

### `shader` (shader.h) — Vertex/Fragment Shader Program
- **What:** Compiles a vertex + fragment shader pair into a linked GPU program. Provides `setMat4`, `setVec3`, etc. to upload uniform values.
- **Why:** Every draw call needs a shader program. Without a wrapper, you'd repeat 30+ lines of compile/check/link/check boilerplate every time.
- **Used by:** Scene rendering (Phong shading), full-screen quad passes (upsampling, compositing), debug voxel visualization.

### `ComputeShader` (ComputeShader.h) — Compute Shader Program
- **What:** Compiles a single compute shader, caches its local work group size, and provides `dispatch(totalX, totalY, totalZ)` that automatically does the ceil-division so you don't have to manually calculate `(total + groupSize - 1) / groupSize` every time.
- **Why:** The smoke system runs **5+ different compute shaders** per frame (voxelize, flood fill, noise, ray march, etc.). Each one needs compile+link+dispatch. The auto ceil-div prevents off-by-one dispatch bugs that cause missing voxels.
- **Used by:** Voxelizer, FloodFill, WorleyNoise, Raymarcher — basically every GPU simulation step.

### `SSBOBuffer` (Buffer.h) — Shader Storage Buffer Object
- **What:** Wraps an OpenGL SSBO — a GPU-side array that compute shaders can read/write. Provides `allocate`, `bindBase(point)`, `upload`, `download`, and `clear`.
- **Why:** The voxel grid is stored as a **flat integer array on the GPU** (one int per voxel, 0-255 density). Compute shaders read/write it using `layout(std430, binding=N)`. We need **3 SSBOs**: one for static walls (read-only after voxelization), and two for smoke density (ping-pong double buffer so we don't read and write the same data in one dispatch). A 4th holds bullet hole data uploaded from CPU each frame.
- **Used by:**
  - Binding 0: Static voxel grid (walls) — written by Voxelizer, read by FloodFill
  - Binding 1-2: Smoke density ping/pong — written/read by FloodFill each step
  - Binding 3: Bullet holes array — uploaded by CPU, read by FloodFill

### `Texture3D` (Texture3D.h) — 3D Volume Texture
- **What:** Creates an immutable 3D texture with `glTexStorage3D`. Can be bound as an **image** (for compute shader `imageStore`/`imageLoad`) or as a **sampler** (for `texture()` lookups with hardware trilinear filtering).
- **Why:** Two key uses:
  1. **Worley noise volume** (128³, `GL_R16F`): The animated noise texture that gives smoke its cloudy look. Generated each frame by a compute shader, then sampled by the ray marcher.
  2. The ray marcher could also copy smoke density into a 3D texture for hardware trilinear sampling (smoother than manual 8-corner interpolation in an SSBO).
- **Why not just use SSBOs for everything?** SSBOs give you raw integer arrays. 3D textures give you **free hardware trilinear interpolation** via `texture()` — critical for smooth-looking volumetric rendering without visible voxel edges.

### `Texture2D` (Texture2D.h) — 2D Texture
- **What:** Same pattern as Texture3D but for 2D. Used as FBO color/depth attachments and as compute shader image outputs.
- **Why needed:**
  - **Ray march output** (half-res `GL_RGBA16F`): RGB = scattered light color, A = transmittance. Written by ray march compute shader.
  - **Scene depth texture** (`GL_DEPTH_COMPONENT32F`): Rendered by rasterizing scene geometry. Read by ray marcher to know where to stop marching (so smoke appears behind walls).
  - **Upsampled result** (full-res): The Catmull-Rom upsampler reads the half-res ray march output and writes a full-res version.

### `Framebuffer` (Framebuffer.h) — Frame Buffer Object
- **What:** An off-screen render target. You attach Texture2D(s) to it, then draw into them instead of the screen.
- **Why:** The rendering pipeline has multiple passes that don't go directly to screen:
  1. **Depth-only FBO**: Render scene geometry to get a depth texture (no color needed). Ray marcher reads this to clip rays.
  2. **Upsample FBO**: Catmull-Rom shader reads half-res smoke, writes full-res result.
  3. **Composite pass**: Final shader blends scene + smoke → writes to screen (default framebuffer 0).
- Without FBOs, you can only render to the screen. Multi-pass rendering requires bouncing between off-screen buffers.

### How they all connect (per frame):

```
CPU: Update flood-fill radius, upload bullet holes
  ↓
[ComputeShader] FloodFill reads/writes SSBOs (ping-pong), checks wall SSBO
  ↓  (memory barrier)
[ComputeShader] WorleyNoise writes → Texture3D (noise volume)
  ↓  (memory barrier)
[Framebuffer+Texture2D] Render scene geometry → depth texture
  ↓
[ComputeShader] Raymarcher reads: smoke SSBO + noise Texture3D + depth Texture2D
                writes: → Texture2D (half-res RGBA smoke)
  ↓  (memory barrier)
[Framebuffer+Shader] Upsample: reads half-res Texture2D → writes full-res Texture2D
  ↓
[Shader] Composite: reads scene color + full-res smoke → draws to screen
```

---

## CMakeLists.txt Skeleton

```cmake
cmake_minimum_required(VERSION 3.20)
project(CS2Smoke CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(glfw3 REQUIRED)
find_package(assimp REQUIRED)
find_package(glm REQUIRED)

add_executable(cs2smoke
    src/main.cpp
    src/core/Window.cpp   src/core/Camera.cpp
    src/gl/Shader.cpp     src/gl/ComputeShader.cpp
    src/gl/Buffer.cpp     src/gl/Texture2D.cpp
    src/gl/Texture3D.cpp  src/gl/Framebuffer.cpp
    src/smoke/Voxelizer.cpp   src/smoke/FloodFill.cpp
    src/smoke/WorleyNoise.cpp src/smoke/Raymarcher.cpp
    src/smoke/BulletHoleManager.cpp src/smoke/SmokeSystem.cpp
    src/post/Upsampler.cpp    src/post/Compositor.cpp
    external/glad/src/glad.c  # ← compile as C
)
target_include_directories(cs2smoke PRIVATE external/glad/include src/)
target_link_libraries(cs2smoke glfw assimp::assimp)
```

---

## SSBO Binding Point Convention (document once, use everywhere)

| Binding | Contents |
|---|---|
| 0 | Static voxels `int[]` (walls, read-only after init) |
| 1 | Smoke voxels read buffer `int[]` (ping) |
| 2 | Smoke voxels write buffer `int[]` (pong) |
| 3 | Bullet holes `GPUBulletHole[]` |

---

## Memory Barrier Reference (most common bug source)

After any compute write, insert the correct barrier **before** the next read:

| Writer | Next Reader | `glMemoryBarrier(...)` bit |
|---|---|---|
| `imageStore` in compute | `imageLoad` in compute | `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT` |
| `imageStore` in compute | `texture()` sampler | `GL_TEXTURE_FETCH_BARRIER_BIT` |
| SSBO write in compute | SSBO read in compute | `GL_SHADER_STORAGE_BARRIER_BIT` |
| SSBO write in compute | `glGetBufferSubData` | `GL_BUFFER_UPDATE_BARRIER_BIT` |
| `glTexSubImage3D` (PBO) | `imageLoad` / `texture()` | `GL_TEXTURE_UPDATE_BARRIER_BIT` |
| FBO rasterizer write | compute `texture()` read | `GL_FRAMEBUFFER_BARRIER_BIT` |

When debugging: use `GL_ALL_BARRIER_BITS` everywhere, then tighten later.

---

## GLSL Image Format Qualifier Reference

The format in `layout(binding=N, <format>)` must **exactly** match `glTexStorage`:

| `glTexStorage` format | GLSL image type | GLSL layout qualifier |
|---|---|---|
| `GL_RGBA16F` | `image2D` / `image3D` | `rgba16f` |
| `GL_R16F` | `image2D` / `image3D` | `r16f` |
| `GL_R32F` | `image2D` / `image3D` | `r32f` |
| `GL_R32I` | `iimage2D` / `iimage3D` | `r32i` |

Use `GL_R32I` / `iimage3D` for the integer voxel grid (stores 0-255 density as int).
Use `GL_R16F` / `image3D` for the Worley noise volume.
Use `GL_RGBA16F` / `image2D` for the ray march output (RGB = scattered light, A = transmittance).

---

## Per-Feature Math Reference

### Feature 1: Mesh Voxelizer
- Grid resolution: `ceil(boundsSize / voxelSize)` per axis
- 1D↔3D index: `idx = x + y*resX + z*resX*resY`
- **Triangle-AABB SAT (13 axes):**
  - 3 cardinal axes (X, Y, Z)
  - 9 cross-product axes: each triangle edge × each cardinal axis
  - 1 triangle face normal
  - For each axis: project triangle vertices and AABB, check interval overlap
  - Any non-overlapping axis → no intersection → voxel is empty
- Use `atomicOr(voxels[idx], 1)` — multiple triangles can hit the same voxel in parallel

### Feature 2: Flood Fill Propagation
- Ping-pong double SSBO to avoid GPU read-write hazards
- Each voxel: `out = max(0, max(6_neighbor_values) - 1)` per step
- Radius constraint: `length((voxelWorldPos - seedPos) / maxRadius) > 1.0 → skip`
- Store density as `int` 0-255 (fixed-point) for atomic safety
- **Easing curve for radius growth (from `Voxelizer.cs`):**
  ```
  if x < 0.5:  ease = 2x²
  else:         ease = 1 - 1/(5*(2x - 0.8) + 1)
  ```

### Feature 3: Volume Ray Marcher
- Ray from camera: `rayDir = normalize(invView * invProj * clipPos)`
- Camera world pos: `invView[3].xyz` (translation column of inverse view)
- **Phase 1 (coarse):** Step 2× voxelSize until non-zero density found
- **Phase 2 (fine):** Fixed step size, accumulate:
  ```
  density = sampleDensity(pos) * volumeDensity
  shadowT = exp(-shadowDensity * extinctionCoeff * shadowStepSize)   ← Beer-Lambert shadow
  color  += lightColor * shadowT * transmittance * phase(g,cosθ) * scatterCoeff * density
  transmittance *= exp(-density * extinctionCoeff * stepSize)        ← Beer-Lambert
  if transmittance < threshold: break                                ← early termination
  ```
- **Beer-Lambert:** `T = exp(-σ_ext × thickness)` — physically correct exponential attenuation
- **Henyey-Greenstein phase:** `(1/4π)(1-g²) / (1+g²-2g·cosθ)^(3/2)` — g∈[-1,1]
- **Trilinear voxel sampling:** 8 corner voxels, `weight = (1-|Δx|)(1-|Δy|)(1-|Δz|)`
- **Depth clipping:** sample scene depth texture, clamp `tMax = sceneDepth`

### Feature 4: Tiled Worley Noise (128³)
- **Worley:** min Euclidean distance to randomly-placed feature point in each cell
- Invert and sharpen: `noise = (1 - minDist)^6`
- Tiling: wrap cell coordinates with modulo when looking up neighbors
- **Hash (Hugo Elias):**
  ```
  n = (n << 13) ^ n
  n = n * (n*n*15731 + 0x789221) + 0x1376312589
  return (n & 0x7FFFFFFF) / 0x7FFFFFFF
  ```
- **fBm layers:** `for i in 0..octaves: noise += amplitude * worley(pos * 2^i + i*warp)`
  - amplitude × 0.5 per octave; warp offsets sample position per octave (swirling)

### Feature 5: SDF Bullet Holes
- **Tapered capsule SDF:** cylinder with r1 at entry, r2 at exit
  ```
  ba = b - a;  h = saturate(dot(pa, ba) / dot(ba, ba))
  closest = pa - h*ba;  r = mix(r1, r2, h)
  sdf = length(closest) - r
  density *= (1 - smoothstep(0, epsilon, -sdf))   // zero inside hole
  ```
- **Easing (from `Gun.cs`):**
  ```
  if t < 0.25:  ease = 1 - (1 - 2t)^15   // explosive start
  else:          ease = 1 - (1.25(t-0.25))² // settle
  ```
- Max 256 holes in SSBO; `GPUBulletHole` = {origin(vec4), forward(vec4), r0, r1, length, elapsed}

### Feature 6: Catmull-Rom Bicubic Upsampling
- Smoke rendered at half/quarter res → upsampled
- **Catmull-Rom weights** for fractional position `f`:
  ```
  w0 =  f*(-0.5 + f*(1.0 - 0.5*f))
  w1 =  1.0 + f²*(-2.5 + 1.5*f)
  w2 =  f*(0.5 + f*(2.0 - 1.5*f))
  w3 =  f²*(-0.5 + 0.5*f)
  ```
- Applied in X and Y to 4×4 neighborhood. C1-continuous (sharper than bilinear, no ringing)

### Feature 7: Composite + Unsharp Mask
- Blend: `finalColor = scene * transmittance + smoke * (1 - transmittance)`
  - Transmittance = alpha channel of ray march output (Beer-Lambert result)
- **Laplacian sharpening (unsharp mask):**
  ```
  laplacian = 4*center - (N + S + E + W)
  sharpened = center + laplacian * strength
  ```
  Counteracts blurring from upsampling

---

## Frame Render Order (SmokeSystem.cpp)

```
Each frame:
1. [CPU] Update flood-fill radius (easing curve)
2. [GPU] CS_FillStep × N: propagate smoke voxels
3. [GPU] CS_GenerateNoise: update 128³ Worley noise texture
4. [GPU] CS_RayMarchSmoke: ray march → smoke color + alpha textures
5. [GPU] Pass 1 (if low res): Catmull-Rom upsample to full res
6. [GPU] Pass 2: Composite + sharpen → final image
7. [GPU] (optional) VisualizeVoxels: draw debug cubes
```

Critical: memory barriers between every GPU→GPU handoff (see barrier table above).

---

## OpenGL Gotchas to Know Before Starting

1. **`glTexStorage3D` is required** for `glBindImageTexture` — mutable `glTexImage3D` causes `GL_INVALID_OPERATION`
2. **SSBO must use `std430`** not `std140` — `std140` pads array elements to 16 bytes (breaks int arrays)
3. **Image format qualifier must match exactly** — `layout(r16f)` ↔ `GL_R16F`, no implicit conversion
4. **Integer textures use `iimage3D`** and `isampler3D`, not `image3D`/`sampler3D`
5. **`glGetProgramiv(id, GL_COMPUTE_WORK_GROUP_SIZE, ls)`** — query local group size after link, cache it
6. **Depth-only FBO** needs `glDrawBuffers(1, &GL_NONE)` + `glReadBuffer(GL_NONE)` or it's incomplete
7. **Don't read depth texture while it's FBO-attached** — unbind FBO first (undefined on AMD)
8. **GLM column-major** — pass to `glUniformMatrix4fv` with `GL_FALSE` (no transpose)
9. **`atomicOr`** in SSBO requires `std430` layout and `coherent` qualifier or explicit barriers
10. **GLAD** must be compiled as a `.c` file, not `.cpp`

---

## Implementation Checklist

### Phase 1: OpenGL Infrastructure

- [ ] **Step 1 — Window + Context**
  - GLFW window, OpenGL 4.3 Core context, GLAD loader
  - Enable `GL_DEBUG_OUTPUT` + debug message callback
  - **Verify:** Window opens, console prints max workgroup count

- [ ] **Step 2 — Shader Wrappers**
  - `Shader` class: compile vert+frag, link, `setInt/setFloat/setVec3/setMat4`
  - `ComputeShader` class: compile+link, `dispatch(totalX,Y,Z)` with auto ceil-div
  - **Verify:** Trivial compute shader writes to SSBO, CPU reads back correct values

- [ ] **Step 3 — SSBO Buffer Wrapper**
  - `SSBOBuffer`: `allocate(bytes)`, `upload<T>()`, `download<T>()`, `bindBase(point)`, `clear()`
  - Use `glClearBufferData` (GL 4.3) for zeroing
  - **Verify:** Write integers via compute, read back on CPU with `glGetBufferSubData`

- [ ] **Step 4 — Texture + Framebuffer Wrappers**
  - `Texture3D`: `glTexStorage3D`, `bindImage(unit, access)`, `bindSampler(unit)`
  - `Texture2D`: same pattern for 2D
  - `Framebuffer`: `attachColor`, `attachDepth`, `isComplete()` check
  - **Verify:** `imageStore` round-trip on a 64³ `GL_R16F` texture

### Phase 2: Core Subsystems

- [ ] **Step 5 — Worley Noise Generator**
  - `worley_noise.comp`: 8×8×8 thread groups, writes to `GL_R16F` `image3D`
  - Hugo Elias hash, tiled cell lookup (wrap with modulo), invert + `^6`
  - fBm: octaves loop with lacunarity + persistence
  - Domain warp per octave (offset by low-freq sine)
  - Animate by adding `time * speed` to sample position
  - `WorleyNoise::generate(float time)` — call every frame
  - **Verify:** Full-screen quad sampling noise volume at z=0.5 shows animated cloud pattern

- [ ] **Step 6 — Mesh Voxelizer**
  - Load mesh with Assimp (`aiProcess_Triangulate | aiProcess_PreTransformVertices`)
  - Upload triangle SSBO: `struct Triangle { vec4 v0, v1, v2; }` (vec4 for std430 alignment)
  - `voxelize.comp`: 1 thread per triangle, inner loop over triangle AABB voxels, `atomicOr`
  - 13-axis SAT in GLSL helper function
  - `Voxelizer::voxelizeMesh(path)` — runs once at startup
  - **Verify:** Download static SSBO, render as instanced cubes — shape should match mesh outline

- [ ] **Step 7 — Flood Fill Propagation**
  - Two SSBOs (ping + pong), swap each step
  - `flood_fill.comp`: 8×8×8 groups, 6-neighbor max+decay, radius constraint, wall check
  - `CS_Seed` kernel: 1×1×1 dispatch, writes 255 at seed coord
  - `FloodFill::seed(worldPos)`, `propagate(int steps)`, `clear()`
  - **Verify:** Seeding at center, visualize SSBO as colored cubes — sphere expands, stops at walls

- [ ] **Step 8 — Camera + Full-Screen Quad**
  - `Camera`: `viewMatrix()`, `projMatrix()`, `invViewMatrix()`, `invProjMatrix()`
  - Camera world pos = `invView[3].xyz`
  - Full-screen quad VAO: 4 verts, clip-space positions + UVs, `GL_TRIANGLE_STRIP`
  - Resize callback: recompute aspect ratio
  - **Verify:** Render a triangle using camera matrices, perspective looks correct

- [ ] **Step 9 — Scene Depth FBO**
  - `GL_DEPTH_COMPONENT32F` texture with `glTexStorage2D`
  - FBO with depth-only attachment (`glDrawBuffers(1, &GL_NONE)`)
  - Render opaque geometry each frame into depth FBO
  - **Verify:** Visualize linearized depth on fullscreen quad — geometry visible

### Phase 3: Rendering

- [ ] **Step 10 — Volume Ray Marcher**
  - `raymarch.comp`: 16×16 thread groups, writes to `GL_RGBA16F` image2D (half-res)
  - Ray setup: reconstruct from `invProj` × `invView` × NDC pixel position
  - Ray-AABB intersection test for volume bounds
  - Phase 1 (coarse skip): jump 2× voxelSize until voxel density > 0.001
  - Phase 2 (fine march): Beer-Lambert accumulation + HG phase + shadow march
  - Trilinear sampling across 8 SSBO voxels (or 3D texture if copied)
  - Modulate by Worley noise sample
  - Clamp ray tMax to scene depth
  - Output: RGB = scattered light, A = final transmittance
  - `Raymarcher::render(...)` — call after flood fill + noise each frame
  - **Verify:** Smoke sphere glows, adjusting `g` changes forward/backward scattering look

- [ ] **Step 11 — SDF Bullet Holes**
  - `struct GPUBulletHole { vec4 origin; vec4 forward; float r0, r1, length, elapsed; }`
  - SSBO of 256 holes, upload each frame
  - Inside `flood_fill.comp`: evaluate tapered capsule SDF per active hole, zero density inside
  - `BulletHoleManager::addHole(origin, dir)`, `update(dt)` (expire after 2s), `uploadToGPU()`
  - Animate radius using easing curve
  - **Verify:** Keyboard press spawns hole, smoke develops a gap that fades over 2 seconds

- [ ] **Step 12 — Catmull-Rom Upsampler**
  - `upsample.frag`: 4×4 Catmull-Rom kernel, weights formula above
  - Clamp transmittance channel to [0,1] after upsampling
  - `Upsampler::upsample(halfResTex, fullResFBO)`
  - **Verify:** Compare bilinear vs Catmull-Rom — cubic has sharper smoke edges

- [ ] **Step 13 — Compositor + Sharpening**
  - `composite.frag`:
    - Laplacian unsharp mask on smoke color before blend
    - `finalColor = scene * transmittance + smokeSharpened * (1 - transmittance)`
  - Debug modes via uniform int: 0=final, 1=smoke only, 2=density mask, 3=depth
  - Render to default framebuffer (id=0)
  - `Compositor::composite(sceneColorTex, fullResTex, debugMode, quadVAO)`
  - **Verify:** Smoke composites naturally over scene, debug modes toggle correctly

### Phase 4: Debug + Integration

- [ ] **Step 14 — Debug Voxel Visualization**
  - `voxel_debug.vert`: reconstruct 3D coord from `gl_InstanceID`, push degenerate if voxels[i]==0
  - Color by density value, unit cube geometry
  - `glDrawArraysInstanced(GL_TRIANGLES, 0, 36, gridX*gridY*gridZ)`
  - Toggle with keyboard key
  - **Verify:** Colored cube grid visible, toggles on/off

- [ ] **Step 15 — SmokeSystem Integration + main.cpp**
  - `SmokeSystem` orchestrates all subsystems in correct frame order (see Frame Order above)
  - Insert all required memory barriers between steps
  - `main.cpp`: GLFW key callbacks for grenade (Space), bullet (F), debug (V, 1/2/3)
  - **Verify:** Full end-to-end frame: smoke spawns, propagates, renders, bullets deform it

---

## Key GLSL Patterns

### Compute shader header template
```glsl
#version 430 core
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(binding = 0, r16f)  uniform image3D  u_Output;
layout(std430, binding = 1) buffer MySSBO { int data[]; };
layout(std140, binding = 0) uniform Params { ivec3 gridSize; float time; };

void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(coord, gridSize))) return; // always bounds-check
    // ...
}
```

### SSBO 3D indexing
```glsl
int flat(ivec3 c, ivec3 sz) { return c.x + c.y*sz.x + c.z*sz.x*sz.y; }
ivec3 unflat(int i, ivec3 sz) {
    return ivec3(i % sz.x, (i / sz.x) % sz.y, i / (sz.x * sz.y));
}
```

### Trilinear SSBO sample
```glsl
float trilinearSample(vec3 worldPos) {
    vec3 g = (worldPos - boundsMin) / voxelSize - 0.5;
    ivec3 c = ivec3(floor(g));
    vec3 t = fract(g);
    // 8 corners
    float v000 = float(voxels[flat(clamp(c,ivec3(0),gridSize-1),gridSize)]);
    // ... repeat for all 8, then:
    return mix(mix(mix(v000,v100,t.x),mix(v010,v110,t.x),t.y),
               mix(mix(v001,v101,t.x),mix(v011,v111,t.x),t.y),t.z) / 255.0;
}
```

### Camera ray reconstruction
```glsl
vec3 getRayDir(vec2 uv, mat4 invProj, mat4 invView) {
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 clip = vec4(ndc, -1.0, 1.0);
    vec4 viewDir = invProj * clip;
    viewDir.w = 0.0;
    return normalize((invView * viewDir).xyz);
}
vec3 cameraPos(mat4 invView) { return invView[3].xyz; }
```

### C++ dispatch helper
```cpp
void ComputeShader::dispatch(int totalX, int totalY, int totalZ) const {
    GLint ls[3];
    glGetProgramiv(m_id, GL_COMPUTE_WORK_GROUP_SIZE, ls);
    glDispatchCompute((totalX+ls[0]-1)/ls[0],
                      (totalY+ls[1]-1)/ls[1],
                      (totalZ+ls[2]-1)/ls[2]);
}
```

---

## Reference: Original Unity Files

The original Unity implementation is in this same repository for reference:

| Unity File | What it does | Ports to |
|---|---|---|
| `Assets/Scripts/Voxelizer.cs` | Voxel grid setup, flood fill control, easing | `src/smoke/Voxelizer.cpp` + `FloodFill.cpp` |
| `Assets/Scripts/Raymarcher.cs` | Noise init, ray march dispatch, post-process | `src/smoke/Raymarcher.cpp` + `post/` |
| `Assets/Scripts/Gun.cs` | Bullet hole creation, SDF easing, GPU upload | `src/smoke/BulletHoleManager.cpp` |
| `Assets/Resources/Voxelize.compute` | SAT voxelizer + flood fill kernels | `shaders/voxelize.comp` + `flood_fill.comp` |
| `Assets/Resources/RenderSmoke.compute` | Worley noise + ray march + SDF | `shaders/worley_noise.comp` + `raymarch.comp` |
| `Assets/Shaders/CompositeEffects.shader` | Catmull-Rom upsample + composite | `shaders/upsample.frag` + `composite.frag` |
| `Assets/Shaders/VisualizeVoxels.shader` | Debug instanced cube rendering | `shaders/voxel_debug.vert/frag` |

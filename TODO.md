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
| **Dear ImGui** | Immediate-mode UI (sliders, buttons, dropdowns) | Clone `ocornut/imgui`, add `backends/imgui_impl_glfw.cpp` + `imgui_impl_opengl3.cpp` to build |
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
│   │   ├── FluidSolver.h / .cpp  ← Divergence → Jacobi pressure → project → advect density
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
    ├── fluid_impulse.comp         ← one-shot radial velocity burst at detonation
    ├── fluid_buoyancy.comp        ← upward force proportional to local smoke density
    ├── fluid_divergence.comp      ← per-voxel ∇·V from 6-face velocity differences
    ├── fluid_pressure.comp        ← single Jacobi pressure iteration (dispatched N×)
    ├── fluid_project.comp         ← subtract ∇p from velocity + zero wall normals
    ├── fluid_advect_density.comp  ← semi-Lagrangian density advection along velocity
    ├── fluid_vorticity.comp       ← vorticity confinement (curl → confinement force)
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
| 4 | Velocity field (ping) `vec4[]` — xyz = velocity, w unused (16-byte aligned) |
| 5 | Velocity field (pong) `vec4[]` |
| 6 | Pressure field (ping) `float[]` |
| 7 | Pressure field (pong) `float[]` |
| 8 | Divergence scratch `float[]` (recomputed every pressure-solve step, no ping-pong needed) |

> **Memory budget (64³ grid):** velocity ×2 = 2 MB, pressure ×2 = 0.5 MB, divergence = 0.25 MB → ~3 MB extra on top of the existing smoke grid.
> **128³:** ~24 MB extra. **256³:** ~196 MB extra — check available VRAM before scaling up.

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

### Feature 4b: Fluid-Driven Dynamic Flow (Navier-Stokes on voxel grid)

The incompressible Navier-Stokes continuity equation requires `∇·V = 0` at every voxel every timestep. The pipeline below enforces this and uses the resulting divergence-free velocity field to advect smoke density.

**Divergence of a single voxel cell (central differences):**
```
∇·V = [(vx[i+1] - vx[i-1]) + (vy[j+1] - vy[j-1]) + (vz[k+1] - vz[k-1])] / (2 × cellSize)
```
Faces adjacent to wall voxels substitute velocity = 0 (no-penetration BC).

**Jacobi pressure update (one iteration, dispatched N times with ping-pong):**
```
p_new = [(pL + pR + pD + pU + pB + pF) - div × cellSize²] / 6
```
Neumann BC at walls: treat missing neighbor pressure as `p_center` (zero gradient). 20 iterations for 64³ gives acceptable convergence; increase to 30 if pressure ripples are visible.

**Velocity projection (enforces incompressibility + wall deflection):**
```
v_new = v_curr - ∇p / cellSize
      = v_curr - vec3(pR-pL, pU-pD, pF-pB) / (2 × cellSize)
```
Post-projection: zero out any velocity component pointing into a solid wall face.

**Semi-Lagrangian advection (stable for any Δt):**
```
prevPos  = worldPos - velocity(worldPos) × Δt      // backtrace one step
density_new[x] = trilinear(density_old, prevPos)   // sample old field at past position
```
Unconditionally stable because we always read from a valid cell. Apply a dissipation multiplier ≈ 0.995 per frame to model natural fading.

**Buoyancy force (smoke rises):**
```
Fy += u_Buoyancy × (density / 255.0) × Δt
```
Optional temperature extension: `Fy += alpha × (T - T_ambient) - beta × density` (Boussinesq approximation). Horizontal drag `v.xz *= (1 - u_HorizDrag × Δt)` prevents lateral drift from accumulating.

**Detonation impulse (one-shot, radial outward burst):**
```
dir      = normalize(voxelWorldPos - detonationPos)
falloff  = smoothstep(u_ImpulseRadius, 0.0, distance)
velocity = dir × u_ImpulseStrength × falloff
```
Inject into velocity ping buffer before the first pressure solve. The solver immediately redistributes this divergent field into divergence-free flow.

**Vorticity confinement (preserve swirling detail vs numerical dissipation):**
```
omega    = curl(v)                              // 6-neighbor central differences
N        = normalize(∇|omega|)                 // gradient of vorticity magnitude
F_vort   = u_VorticityScale × cross(N, omega) × Δt
v_new   += F_vort
```
The cross product `N × omega` is a tangential force that tightens existing vortex tubes. Without it, smoke settles into smooth laminar flow within seconds. With it, rolling boundary structures persist across the full lifetime of the grenade.

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

> **[UPDATED — commit 71a44e9]** The render loop now calls `ProceduralSmokeSystem::update()` instead of separate `floodFill.propagate()` + `solver.step()` calls. The actual per-frame GPU sequence is documented below.

```
Each frame (via ProceduralSmokeSystem::update()):

  -- Step 1: Expand smoke source region --
  [GPU]  FloodFill::propagate(stepsPerFrame=15)
           reads/writes flood fill ping-pong SSBOs; blocked by wall SSBO

  -- Step 2: Inject flood fill into smoke simulation buffers --
  [GPU]  FloodFillToSmoke.comp  (FloodFillToSmoke::injectSmoke)
           reads: floodFillSrc (int), walls (int), smokeDensitySrc (float)
           writes: smokeDensityDest (float)
           rule:  smokeDensityDest[i] = max(oldSmoke, floodNorm)
           reason: treats flood fill region as a persistent source — voxels stay full
  [GPU]  FloodFillToVelocity.comp  (FloodFillToSmoke::injectVelocity)
           reads: floodFillSrc (int), walls (int), velocitySrc (vec4)
           writes: velocityDest (vec4)
           rule:  vel += normalize(coord - seedCoord) * injectStrength * floodNorm
           reason: drives radial outward expansion, strongest at seed, weaker at edges
           NOTE: injection is ADDITIVE every frame (continuous emitter model, not one-shot)
  [CPU]  smoke.swapVelocity(), smoke.swapDensity()

  -- Step 3: Run fluid solver on updated smoke state --
  [GPU]  SmokeSolver::step():
    3a. ComputeDivergence   — compute ∇·V per voxel → divergence scratch buffer
    3b. PressureJacobi × 60  — Jacobi pressure iterations (ping-pong, 60 iters default)
    3c. ProjectVelocity     — subtract ∇p * dt from velocity; apply velDecay=0.995
    3d. AdvectSmoke         — semi-Lagrangian density advection along velocity field
    3e. AdvectVelocity      — semi-Lagrangian velocity self-advection

  GL_SHADER_STORAGE_BARRIER_BIT between each dispatch above.

Then (outside ProceduralSmokeSystem):
4. [GPU]  WorleyNoise::generate(time)  — update 128³ Worley noise texture
5. [GPU]  Raymarcher::render(smoke.getSrcDensity(), ...)
           reads the float density SSBO from step 3d output (not the flood fill int buffer)
6. [GPU]  (future) Catmull-Rom upsample → full-res
7. [GPU]  (future) Composite + sharpen → final image
8. [GPU]  (optional) VisualizeVoxels: draw debug cubes
```

> **Still TODO from original plan (not yet in render loop):**
> - `fluid_buoyancy.comp` — upward force proportional to local smoke density
> - `fluid_vorticity.comp` — vorticity confinement to preserve swirling detail
> - One-shot detonation impulse (`fluid_impulse.comp`) — currently replaced by continuous `FloodFillToVelocity`

Critical: `GL_SHADER_STORAGE_BARRIER_BIT` between every GPU→GPU handoff (see barrier table above).

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

- [x] **Step 10a — Ray March Core: Beer-Lambert** *(implemented; smoke source updated — commit 71a44e9)*

  > **[UPDATED — commit 71a44e9]** The smoke density source was changed from the flood fill `int` buffer to the fluid simulation's `float` density SSBO (`smoke.getSrcDensity()`). The `u_MaxDensityVal` uniform and the `/float(max(u_MaxDensityVal,1))` normalization in `sampleSmoke()` were removed — the float field is already `[0,1]` normalized. The `Raymarcher::render()` signature was simplified: the shape/seed parameters (`seedWorldPos`, `maxSeedVal`, `radiusXZ`, `radiusY`) were removed entirely since smoke shape now comes from the simulation, not an ellipsoid formula.

  - `Raymarcher.h`: class with `Texture2D smokeOut` (half-res `GL_RGBA16F`), `ComputeShader marchCS`, and a blit vert+frag shader pair to draw result to screen
  - `raymarch.comp`: `layout(local_size_x=16, local_size_y=16)`, one thread per half-res pixel
  - **Ray setup:** reconstruct ray from camera matrices
    ```glsl
    vec2 ndc  = (vec2(px) + 0.5) / vec2(u_TexSize) * 2.0 - 1.0;
    vec4 vDir = u_InvProj * vec4(ndc, -1.0, 1.0);  vDir.w = 0.0;
    vec3 rayDir = normalize((u_InvView * vDir).xyz);
    vec3 rayOrigin = u_InvView[3].xyz;   // camera world pos = last column of invView
    ```
  - **Ray-AABB slab test** against smoke volume bounds (`u_BoundsMin`, `u_BoundsMax`):
    ```glsl
    vec3 t1 = (u_BoundsMin - rayOrigin) / rayDir;
    vec3 t2 = (u_BoundsMax - rayOrigin) / rayDir;
    float tEnter = max(max(min(t1.x,t2.x), min(t1.y,t2.y)), min(t1.z,t2.z));
    float tExit  = min(min(max(t1.x,t2.x), max(t1.y,t2.y)), max(t1.z,t2.z));
    if (tExit < 0.0 || tEnter > tExit) { imageStore(..., vec4(0)); return; }
    tEnter = max(tEnter, 0.0);
    ```
  - **Trilinear SSBO sampling** — smoke SSBO stores int [0..u_MaxVoxelVal], normalise to [0,1]:
    ```glsl
    float sampleSmoke(vec3 worldPos) {
        vec3 gc = (worldPos - u_BoundsMin) / u_VoxelSize - 0.5;
        ivec3 c0 = clamp(ivec3(floor(gc)),   ivec3(0), u_GridSize-1);
        ivec3 c1 = clamp(ivec3(floor(gc))+1, ivec3(0), u_GridSize-1);
        vec3 t = fract(gc);
        // read 8 corners: v000..v111 = float(smoke[flatIdx(ivec3(cx,cy,cz))])
        return trilinear(v000..v111, t) / float(u_MaxVoxelVal);
    }
    ```
  - **Two-phase march:**
    - Phase 1 (coarse skip): step `u_VoxelSize * 2.0` until `sampleSmoke() > 0.002`, max 96 steps
    - Phase 2 (fine accumulation): step `u_VoxelSize * 0.5`, max 192 steps
    - **Beer-Lambert accumulation** (pure extinction, no phase yet):
      ```glsl
      float density = sampleSmoke(pos) * u_DensityScale;
      float stepT   = exp(-density * u_SigmaE * u_StepSize);  // Beer-Lambert per step
      transmittance *= stepT;
      color         += vec3(density) * transmittance * u_StepSize;  // placeholder lighting
      if (transmittance < 0.01) break;                               // early exit
      ```
  - Output: `imageStore(u_Output, px, vec4(color, transmittance))`
    - RGB = accumulated scattered light, A = final transmittance (for later compositor)
  - **Blit pass:** simple vert+frag that draws `smokeOut` to screen:
    `fragColor = vec4(bg * smoke.a + smoke.rgb, 1.0)` where bg = sky/background colour
  - Expose `u_DensityScale` and `u_SigmaE` as CPU-side tweakable floats
  - `Raymarcher::render(camera, floodFill, voxelizer)` — call each frame after flood fill
  - `Raymarcher::blit(fsQuad)` — draws result to screen; toggle with R key in main.cpp
  - **Verify:** Smoke mass visible as grey/white blob; thicker regions correctly darker

- [x] **Step 10b — Worley Noise Modulation + Domain Warp at Edges** *(simplified from original plan — commit 71a44e9)*

  > **[UPDATED — commit 71a44e9]** The original curl-warp approach (three staggered noise lookups → `[-1,1]` warp vector → edge-masked warp → warped sample) was commented out rather than deleted. It is preserved in `Raymarch.comp` for reference. The replacement is a simpler perturbation: `density = baseDensity * (1 + noiseVal * u_NoiseStrength * 0.4) * u_DensityScale`, where `noiseVal` is a single `texture()` lookup remapped to `[-1,1]`. This avoids the earlier issue where full-strength curl warp was destroying the smoke density entirely. The `edgeMask`, `u_EdgeFadeWidth`, `u_CurlStrength` uniforms remain in the shader for future reactivation.

  - Bind the existing Worley 128³ `GL_R16F` texture as `sampler3D` (binding 2) in the march shader
  - **Edge-zone mask:** the stored voxel density IS the Euclidean falloff value [0..1];
    use it directly to define the "edge zone" (low density = boundary of smoke):
    ```glsl
    float rawDensity = sampleSmoke(pos);          // 0..1 Euclidean falloff
    float edgeMask   = 1.0 - smoothstep(0.0, u_EdgeFadeWidth, rawDensity);
    // edgeMask = 1 at outer edge (low density), 0 at core (high density)
    ```
  - **Domain warp (curl-like):** perturb the noise sample position at edges to create turbulent boundary;
    use the noise texture itself at a different scale/phase to generate a 3D warp offset:
    ```glsl
    vec3 noiseUVW = pos / (u_BoundsMax - u_BoundsMin) + u_Time * 0.04;
    vec3 warp = vec3(
        texture(u_NoiseTex, noiseUVW + vec3(0.0,  0.0,  0.0)).r,
        texture(u_NoiseTex, noiseUVW + vec3(0.43, 0.17, 0.0)).r,
        texture(u_NoiseTex, noiseUVW + vec3(0.0,  0.43, 0.17)).r
    ) * 2.0 - 1.0;                                  // remap [0,1] -> [-1,1]
    vec3 warpedPos = pos + warp * edgeMask * u_CurlStrength * u_VoxelSize;
    float noiseVal = texture(u_NoiseTex, warpedPos / (u_BoundsMax - u_BoundsMin)).r;
    ```
    The three texture lookups at staggered offsets approximate a curl-like divergence-free
    displacement without needing a separate curl noise texture.
  - **Combine density + noise:**
    ```glsl
    // max(euclidean, voxel distance) falloff — the stored voxel value already IS the
    // euclidean-normalised distance; use max to prevent noise from adding density where
    // the flood fill has not yet reached (ensures walls still block cleanly):
    float density = max(rawDensity, 0.0) * mix(1.0, noiseVal, edgeMask * u_NoiseStrength);
    ```
  - Expose `u_EdgeFadeWidth` (default 0.3), `u_CurlStrength` (default 1.5), `u_NoiseStrength` (default 0.9)
  - **Verify:** Smoke boundary looks fluffy/turbulent; interior remains smooth; walls still block

- [ ] **Step 10c — Split Extinction: Scattering (σ_s) + Absorption (σ_a) + Shadow Ray**
  - Replace single `u_SigmaE` with two uniforms: `u_SigmaS` (scattering) and `u_SigmaA` (absorption)
    - Extinction: `float sigmaE = u_SigmaS + u_SigmaA;`
    - Smoke albedo = `u_SigmaS / sigmaE` (high albedo ≈ 0.9 → white/grey smoke; low → dark smoke)
  - **Shadow ray** — before accumulating scattered light, march a short ray from `pos` toward the light
    to compute how much light is attenuated before reaching that point:
    ```glsl
    float shadowMarch(vec3 pos, vec3 lightDir, int steps, float stepSize) {
        float opticalDepth = 0.0;
        for (int i = 0; i < steps; i++) {
            pos += lightDir * stepSize;
            if (!insideBounds(pos)) break;
            opticalDepth += sampleDensityWithNoise(pos) * u_DensityScale;
        }
        return exp(-opticalDepth * sigmaE * stepSize);  // Beer-Lambert shadow transmittance
    }
    ```
    - Use 8 shadow steps at `u_VoxelSize * 1.5` step size (cheap but plausible)
  - **Powder effect** (fake multiple scattering — makes dense interior darker, avoids flat look):
    ```glsl
    float powder = 1.0 - exp(-density * sigmaE * u_StepSize * 2.0);
    float lightMult = 2.0 * powder;   // brightens edges, darkens core
    ```
  - **Updated accumulation** (replaces Step 10a placeholder):
    ```glsl
    float shadowT = shadowMarch(pos, u_LightDir, 8, u_VoxelSize * 1.5);
    vec3  Li      = u_LightColor * shadowT * lightMult;   // incoming radiance at sample
    color        += transmittance * u_SigmaS * density * Li * u_StepSize;
    transmittance *= exp(-sigmaE * density * u_StepSize);
    ```
  - Expose `u_LightDir` (world-space, normalised), `u_LightColor`, `u_SigmaS`, `u_SigmaA`
  - **Verify:** Lit side of smoke brighter; shadowed interior visibly darker; thicker smoke self-shadows

- [ ] **Step 10d — Phase Function: Henyey-Greenstein vs Rayleigh**
  - Implement both phase functions in the shader, selected via `uniform int u_PhaseMode` (0 = HG, 1 = Rayleigh):
    ```glsl
    const float PI = 3.14159265;
    float cosTheta = dot(rayDir, u_LightDir);   // angle between view ray and light

    // Henyey-Greenstein (smoke particles, Mie scattering regime)
    // g in [-1,1]: 0=isotropic, >0=forward, <0=backward. Smoke: g~0.4
    float phaseHG(float cosT, float g) {
        float g2 = g * g;
        return (1.0 / (4.0*PI)) * (1.0 - g2) / pow(1.0 + g2 - 2.0*g*cosT, 1.5);
    }

    // Rayleigh (molecular/small-particle scattering — blue-sky, fog at large scale)
    // Symmetric lobe: forward = backward, brighter at 0 deg and 180 deg
    float phaseRayleigh(float cosT) {
        return (3.0 / (16.0*PI)) * (1.0 + cosT*cosT);
    }

    float phase = (u_PhaseMode == 0) ? phaseHG(cosTheta, u_G) : phaseRayleigh(cosTheta);
    ```
  - Multiply `phase` into scattered light: `color += transmittance * u_SigmaS * density * Li * phase * u_StepSize;`
  - Expose `u_G` (default 0.4 for smoke), `u_PhaseMode` (toggle with P key)
  - **Expected difference:**
    - HG g=0.4: smoke brighter when camera looks toward the light (forward-scatter lobe)
    - HG g=0.0: isotropic baseline, good reference
    - Rayleigh: two symmetric lobes (front+back), no single forward spike, looks more like haze
    - Smoke is in the Mie regime (particles >> wavelength) so HG is physically correct;
      Rayleigh is useful for comparison and for haze/atmosphere effects
  - **Verify:** Toggle P key changes visible glow direction; HG shows strong rim-light when backlit

- [x] **Step 10e — Fluid-Driven Dynamic Smoke Flow** *(partially implemented — commit 71a44e9)*

  > **[IMPLEMENTED]** The core fluid pipeline is now live. See design notes below before reading the original plan — several pieces were implemented differently from the original spec.
  >
  > **What was built (commit 71a44e9):**
  >
  > - **`AdvectSmoke` / `AdvectSmoke.comp`** — semi-Lagrangian smoke density advection. Added to `SmokeSolver::step()` after `ProjectVelocity`. Each voxel backtraces `worldPos - vel * dt`, trilinearly samples the old density field, and writes the result to the dest buffer. This is the step that makes density *move* with the velocity field.
  >
  > - **`FloodFillToSmoke.comp`** — converts the integer flood fill budget field into a normalized `[0,1]` float smoke density source. Uses `max(oldSmoke, floodNorm)` so any voxel the flood fill has reached stays dense (persistent source, not a one-shot seed). Run once per frame before the solver.
  >
  > - **`FloodFillToVelocity.comp`** — injects radial outward velocity into every flood-fill-reached voxel. Direction is `normalize(coord - seedCoord)`, magnitude scales with `floodNorm` (strong near seed, weak at frontier). **Design decision:** injection is ADDITIVE every frame (continuous emitter model). This means velocity accumulates in the source region over time, driving sustained expansion. If smoke appears "too explosive", revisit this in `FloodFillToSmoke::injectVelocity()`. The code comment flags this explicitly.
  >
  > - **`FloodFillToSmoke` C++ class** (`src/Procedural/FloodFillToSmoke.h/.cpp`) — wraps both shaders. Exposes `injectAll()` which fires smoke injection then velocity injection in sequence, with a `GL_SHADER_STORAGE_BARRIER_BIT` after each. Tunable via `setVelocityInjectStrength()` / `setSmokeDenseInjectStrength()`.
  >
  > - **`ProceduralSmokeSystem` C++ class** (`src/Procedural/ProceduralSmokeSystem.h/.cpp`) — top-level orchestrator called once per frame from `main.cpp`. Its `update()` method runs: flood fill propagation → `FloodFillToSmoke::injectAll()` → `SmokeSolver::step()`. Exposes all tunable hyper-parameters (steps-per-frame, inject strengths) as setters. This replaces the separate `floodFill.propagate()` + `solver.step()` calls that previously lived directly in `main.cpp`.
  >
  > - **`ProjectVelocity` updated** — now takes `float dt` parameter, scales pressure gradient subtraction by `dt` for frame-rate independence. A `velDecay = 0.995` factor is applied every frame (hardcoded; comment says to expose via `ProceduralSmokeSystem`).
  >
  > - **Jacobi pressure iterations doubled** — `DEFAULT_ITER_COUNT` increased from 30 to 60 to improve divergence-free enforcement with the now-active velocity field.
  >
  > **Still TODO from original plan:**
  > - `fluid_buoyancy.comp` — upward lift proportional to local density
  > - `fluid_vorticity.comp` — vorticity confinement for swirling boundary detail
  > - True one-shot detonation impulse (currently replaced by continuous FloodFillToVelocity injection)
  > - Expose `velDecay` and Jacobi iter count as `ProceduralSmokeSystem` setters

  The flood fill gives smoke its wall-aware shape, but motion is static. This step hooks the GPU Navier-Stokes pipeline (divergence → Jacobi pressure → velocity projection → advection) into the simulation loop so smoke actively flows outward on detonation, redirects when it hits walls, rises under buoyancy, and retains visible swirling detail.

  **New SSBOs required:** bindings 4–8 (see binding table above). Allocate in `SmokeSystem` alongside existing smoke SSBOs.

  **`fluid_impulse.comp` — Detonation velocity burst (one-shot)**
  - Dispatch once when the grenade is triggered, before the first flood-fill step
  - For each non-wall voxel: compute `dir = normalize(voxelWorldPos - detonationPos)`, `dist = length(...)`, then write `velocity[i] = vec4(dir * u_ImpulseStrength * smoothstep(u_ImpulseRadius, 0.0, dist), 0.0)` to the ping buffer
  - `u_ImpulseStrength` ≈ 8–15 world-units/s; `u_ImpulseRadius` ≈ half the grid radius
  - **Why:** gives the pressure solver a divergent initial field to immediately redistribute, driving believable outward expansion in the first dozen frames rather than a uniform blob

  **`fluid_buoyancy.comp` — Upward force from local density**
  - Run every frame, before the divergence step
  - `velocity[i].y += u_Buoyancy * (float(smokeGrid[i]) / 255.0) * u_DeltaTime`
  - Start with `u_Buoyancy` ≈ 1.5; smoke will visibly drift upward relative to the floor
  - **Gravity extension:** subtract a downward drag term on the x/z components to keep smoke from drifting laterally under numerical error: `velocity[i].xz *= (1.0 - u_HorizDrag * u_DeltaTime)` where `u_HorizDrag` ≈ 0.2

  **`fluid_divergence.comp` — Per-voxel divergence**
  - Computes `div[i] = (vx[right] - vx[left] + vy[up] - vy[down] + vz[front] - vz[back]) / (2 * cellSize)`
  - For faces adjacent to a wall voxel, treat that neighbor's velocity component as 0 (no-penetration BC — bakes wall blocking directly into the divergence)
  - Write result to binding 8 (`float divergence[]`)

  **`fluid_pressure.comp` — Jacobi pressure iteration (dispatched N× per frame)**
  - Single iteration: `p_new[i] = (pL + pR + pD + pU + pB + pF - divergence[i] * cellSize²) / 6.0`
  - Neumann BC at solid walls: if a neighbor voxel is a wall, substitute `p_center` for that neighbor's pressure (zero normal gradient — prevents pressure leaking into geometry)
  - Ping-pong between bindings 6 and 7; swap binding pointers on CPU, not on GPU
  - **Iteration count:** 20 iterations for a 64³ grid fits comfortably in 4–5 ms. Reduce to 15 if total frame time exceeds budget. 30+ is unnecessary at this resolution

  **`fluid_project.comp` — Subtract pressure gradient + enforce wall BCs**
  - `velocity[i].xyz -= (vec3(pR-pL, pU-pD, pF-pB) / (2.0 * cellSize))`
  - After subtraction, zero out any velocity component that points into an adjacent wall: if the `+x` neighbor is a wall and `velocity[i].x > 0`, set `velocity[i].x = 0`. Repeat for all 6 faces.
  - This is the step that produces the wall-deflection effect: smoke that would penetrate a surface instead turns and flows along it

  **`fluid_advect_density.comp` — Semi-Lagrangian density transport**
  - For each fluid voxel: backtrace `prevPos = worldPos - velocity[i].xyz * u_DeltaTime`; trilinear-sample `smokeGrid` at `prevPos` (the same 8-corner lerp used in the ray marcher); write result to the smoke pong buffer; multiply by `u_Dissipation` (≈ 0.995) to model slow fading
  - **Why semi-Lagrangian:** forward advection (`newGrid[pos + v*dt] += density`) scatters values, causes gaps, and can explode. Pulling from the past is unconditionally stable regardless of timestep
  - Run **instead of** the regular flood-fill propagation step once the velocity field is initialised; the flood fill only seeds the initial density blob at detonation

  **`fluid_vorticity.comp` — Vorticity confinement (optional but impactful)**
  - Compute `omega = curl(v)` at each voxel via central differences over 6 neighbors:
    ```glsl
    omega.x = (v[y+1].z - v[y-1].z - v[z+1].y + v[z-1].y) * invH;
    omega.y = (v[z+1].x - v[z-1].x - v[x+1].z + v[x-1].z) * invH;
    omega.z = (v[x+1].y - v[x-1].y - v[y+1].x + v[y-1].x) * invH;
    ```
  - Compute gradient of `|omega|` (another 6-neighbor pass), normalize to get `N`
  - Apply: `velocity[i].xyz += cross(N, omega) * u_VorticityScale * u_DeltaTime`
  - `u_VorticityScale` ≈ 0.2–0.4 for 64³; higher values produce tighter visible swirls but can go unstable above 0.8
  - Run after buoyancy, before divergence. This is the step that counteracts numerical dissipation — without it the simulation converges to a smooth laminar flow after a few hundred frames; with it, rolling smoke balls and edge vortices persist
  - **Why it matters for wall interaction:** when smoke deflects along a wall, vorticity confinement amplifies the shear layer at the deflection boundary, creating visible smoke rolls along the wall face — much closer to real CS2 behaviour

  **`FluidSolver` C++ class**
  - Owns the 5 new SSBOs (vel ping/pong, pressure ping/pong, divergence)
  - Public API: `seed(detonationWorldPos)` (one-shot impulse), `step(deltaTime, jacobiIters)` (per-frame)
  - `step()` order: buoyancy → vorticity confinement → divergence → Jacobi ×N → project → advect density
  - Insert all required `GL_SHADER_STORAGE_BARRIER_BIT` barriers between each dispatch

  **Integration with existing SmokeSystem frame order:**
  ```
  [FRAME START]
  1. CPU: update flood-fill radius (kept for initial shape seeding only)
  2. GPU: FloodFill × 1 step (seeds density; skip once radius is fully expanded)
  3. GPU: FluidSolver::step()
       3a. fluid_buoyancy       ← adds upward velocity
       3b. fluid_vorticity      ← amplifies existing rotation
       3c. fluid_divergence     ← measures how non-divergence-free velocity is
       3d. fluid_pressure × 20  ← Jacobi solve
       3e. fluid_project        ← make velocity divergence-free; deflect at walls
       3f. fluid_advect_density ← move smoke density along solved velocity
  4. GPU: WorleyNoise update
  5. GPU: Raymarch (reads advected density from step 3f output)
  ...
  ```

  **Expected visual improvements:**
  - **Expansion:** smoke puffs outward in a pressure-wave pattern rather than uniform sphere inflation
  - **Wall reaction:** smoke flowing into a wall pressure-stalls, turns, and streams along the surface — visible as a fan-out sheet at the wall face
  - **Gravity / buoyancy:** the cloud tilts upward over time; the bottom edge thins before the top
  - **Turbulence:** vorticity confinement causes visible swirling rolls at the cloud boundary, matching the billowing behaviour in the reference video
  - **Verify:** seed grenade near a wall corner; smoke should bend around the corner and fill both sides over 2–3 seconds rather than linearly spreading

- [ ] **Step 11 — Catmull-Rom Upsampler**
  - `upsample.frag`: 4×4 Catmull-Rom kernel, weights formula above
  - Clamp transmittance channel to [0,1] after upsampling
  - `Upsampler::upsample(halfResTex, fullResFBO)`
  - **Verify:** Compare bilinear vs Catmull-Rom — cubic has sharper smoke edges

- [ ] **Step 12 — Compositor + Sharpening**
  - `composite.frag`:
    - Laplacian unsharp mask on smoke color before blend
    - `finalColor = scene * transmittance + smokeSharpened * (1 - transmittance)`
  - Debug modes via uniform int: 0=final, 1=smoke only, 2=density mask, 3=depth
  - Render to default framebuffer (id=0)
  - `Compositor::composite(sceneColorTex, fullResTex, debugMode, quadVAO)`
  - **Verify:** Smoke composites naturally over scene, debug modes toggle correctly

### Phase 4: Debug + Integration

- [ ] **Step 14 — Camera Controls + ImGui Parameter UI**

  **Part A — Camera movement** *(WASD implemented — see note below)*

  > **[PARTIALLY IMPLEMENTED]** WASD + Q/E camera movement is live in `main.cpp` render loop (checked via `glfwGetKey` each frame for smooth continuous movement). Movement is relative to camera orientation, flattened to XZ so W/S don't cause unintended vertical drift. Speed scales with `g_camera.dist` so it feels consistent at any zoom level.
  >
  > **Current key bindings (no conflicts):**
  > - `W/S` — move forward/back
  > - `A/D` — strafe left/right
  > - `Q/E` — move down/up
  > - `Left-drag` — orbit (unchanged)
  > - `Shift + left-drag` — pan target (unchanged)
  > - `Scroll` — zoom (unchanged)
  > - `L + left-drag` — move light source in XZ plane (ray-plane unproject)
  > - `L + scroll` — move light source up/down (Y)
  > - `N` — noise debug, `V` — velocity debug, `R` — raymarch toggle
  > - `F` — depth framebuffer debug *(was `D`, changed to avoid conflict with WASD)*
  >
  > **Still TODO from original Part A plan:**
  > - Right-click mouse capture mode (hide cursor + raw input) for look-around; currently left-drag orbits instead of free-look
  > - Scroll-to-adjust-speed multiplier

  **Part B — Dear ImGui integration**
  > **[IMPLEMENTED]** ImGui v1.92.6 integrated.
  > - Core sources (`imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`) and most backends compiled from `includes/imgui/` (excluded from git — teammates clone separately per README).
  > - All imgui backend files used directly from `includes/imgui/backends/` — no modifications. Old bundled `includes/GLFW/` (v3.0.4) removed; system GLFW 3.3.10 now used everywhere, which resolved the original `LastMouseCursor` compile error.
  > - Init/shutdown and per-frame NewFrame/Render calls wired into `main.cpp`.
  > - `ImGui::GetIO().WantCaptureMouse` checked to prevent camera stealing clicks when over UI.

  **Part C — Parameter sliders (single `ImGui::Begin("Smoke Grenade")` window)**

  Group into collapsible sections with `ImGui::CollapsingHeader`:

  *Grenade Controls*
  - `ImGui::Button("Throw Grenade [Space]")` — same action as Space key; resets simulation and seeds at aim point
  - `ImGui::Button("Reset")` — clears smoke + velocity field without re-throwing
  - `ImGui::SliderFloat("Expansion Speed", &floodFillStepsPerFrame, 1, 8)` — how many flood-fill steps run per frame; lower = slower cinematic expand, higher = instant fill

  *Smoke Volume*
  - `ImGui::SliderFloat("Density Scale", &u_DensityScale, 0.1, 5.0)` — overall opacity; low = wispy, high = opaque wall
  - `ImGui::SliderFloat("Scattering σ_s", &u_SigmaS, 0.1, 10.0)` — how much light bounces inside the cloud (higher = brighter, whiter smoke)
  - `ImGui::SliderFloat("Absorption σ_a", &u_SigmaA, 0.0, 5.0)` — how much light is absorbed (higher = darker, denser-looking smoke)
  - `ImGui::SliderFloat("Dissipation", &u_Dissipation, 0.98, 1.0, "%.4f")` — per-frame density decay from advection; 1.0 = smoke never fades, 0.98 = fades in ~2 s

  *Phase Function (light scattering shape)*
  - `ImGui::RadioButton("Henyey-Greenstein", &u_PhaseMode, 0)` / `ImGui::RadioButton("Rayleigh", &u_PhaseMode, 1)` — side by side; HG is physically correct for smoke, Rayleigh looks more like haze/fog
  - `ImGui::SliderFloat("HG Anisotropy g", &u_G, -1.0, 1.0)` — only relevant when HG selected; 0 = uniform glow, 0.4 = forward scatter (bright toward light), negative = backscatter; **this one has the most dramatic visual effect**

  *Noise & Edge Detail*
  - `ImGui::SliderFloat("Noise Strength", &u_NoiseStrength, 0.0, 1.0)` — how much Worley noise breaks up the smoke boundary; 0 = smooth sphere, 1 = fully fluffy
  - `ImGui::SliderFloat("Edge Fade Width", &u_EdgeFadeWidth, 0.05, 0.6)` — how wide the noisy transition zone is at the cloud boundary
  - `ImGui::SliderFloat("Curl Strength", &u_CurlStrength, 0.0, 4.0)` — how much domain warp distorts the edge (higher = more turbulent wisps)
  - `ImGui::SliderFloat("Noise Speed", &u_NoiseSpeed, 0.0, 0.2)` — animation rate of the Worley texture; 0 = frozen, 0.05 = slow drift, 0.2 = fast boiling

  *Fluid Simulation*
  - `ImGui::SliderFloat("Buoyancy", &u_Buoyancy, 0.0, 5.0)` — upward lift; 0 = smoke stays flat on the ground, 3+ = smoke rises quickly to ceiling
  - `ImGui::SliderFloat("Vorticity Scale", &u_VorticityScale, 0.0, 1.0)` — swirling intensity at cloud edges; 0 = smooth diffusion, 0.5+ = visible rolling vortex rings
  - `ImGui::SliderInt("Pressure Iterations", &u_JacobiIters, 5, 40)` — higher = more accurate divergence-free flow (better wall deflection) at the cost of GPU time; 20 is the default sweet spot
  - `ImGui::SliderFloat("Impulse Strength", &u_ImpulseStrength, 1.0, 30.0)` — initial explosion velocity; higher = smoke punches outward more aggressively before settling

  *Lighting*

  > **[IMPLEMENTED]** Full `LightSource` class (`src/Rendering/LightSource.h`) with ImGui panel ("Light Source" window, 300px wide):
  > - **Azimuth / Elevation sliders** — spherical coordinate control; `rebuildPosition()` converts to XYZ each frame. `syncAngles()` called after mouse drag to keep sliders in sync.
  > - **Day ↔ Night slider** — blends noon (warm white) → sunset (orange) → night (cool blue) across `[0, 1]`. Tints wall/geometry lighting via `u_LightColor` in VoxelDebug shader. Smoke raymarcher intentionally kept at fixed warm white (Henyey-Greenstein not yet implemented — proper in-scattering tint deferred to Step 10c/d).
  > - **Intensity slider** — `[0, 3]`, multiplied into `getColor()`.
  > - **Ambient slider** — `[0, 0.8]`; `u_Ambient` uniform wired into VoxelDebug fragment shader replacing hardcoded `0.35`. Controls base illumination on surfaces facing away from the light.
  > - **Orbit checkbox + speed slider** — checkbox toggles `orbitEnabled`; speed slider shown only when orbit is on.
  > - **Mouse drag** — `L + left-drag` moves light in world XZ via ray-plane unproject (exact cursor tracking). `L + scroll` adjusts Y height.
  > - **Visual marker** — 3-layer point sprite (halo/glow/core) always rendered at light position.

  - **Verify:** Drag HG g from -1 → +1 while looking toward the light — the bright lobe should visibly swing from backlit rim to front-facing; drag Buoyancy to 3 and watch smoke curl toward the ceiling

- [ ] **Step 15 — SmokeSystem Integration + main.cpp**
  - `SmokeSystem` orchestrates all subsystems in correct frame order (see Frame Order above)
  - Insert all required memory barriers between steps
  - `main.cpp` key bindings:
    - `Space` — throw grenade at aim point (also available via ImGui button)
    - `F` — fire bullet (SDF hole)
    - `Right Mouse` — toggle mouse capture for camera look
    - `W/A/S/D`, `Q/E` — camera movement (only when mouse is captured)
    - `Scroll` — camera speed multiplier
    - `Escape` — release mouse / close
  - All smoke parameters live in a plain `SmokeParams` struct on the CPU; upload changed values to shader uniforms each frame (only re-upload if dirty flag set)
  - **Verify:** Full end-to-end frame: smoke spawns, propagates, renders, bullets deform it; ImGui window visible and sliders update smoke in real time

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

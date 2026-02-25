# CS2 Volumetric Smoke Grenade — Implementation Report

**Course:** 50.017 Graphics and Visualisation
**Project:** Real-Time Volumetric Smoke using OpenGL 4.3 Compute Shaders
**Reference System:** Gunnell, G. (2023). *CS2 Smoke Grenades* [Open-source Unity recreation]. GitHub. https://github.com/GarrettGunnell/CS2-Smoke-Grenades

---

## Abstract

This report describes the GPU implementation of a real-time volumetric smoke grenade system in OpenGL 4.3/C++, reproducing the technique introduced by Valve in *Counter-Strike 2* (2023). The system voxelizes a static scene using the Separating Axis Theorem (SAT), propagates a smoke volume outward from a detonation point using a decay-based flood fill, animates it with tiled 3D Worley cellular noise, and renders the resulting density field volumetrically using Beer-Lambert ray marching with the Henyey-Greenstein phase function. Each subsystem runs as a GPU compute shader, operating on shared SSBO and 3D texture resources with no CPU readback during the render loop.

---

## 1. Introduction

Volumetric effects such as smoke, fog, and fire belong to the class of *participating media*: materials that absorb, emit, and scatter light along a ray rather than only at a surface. Physically accurate simulation requires solving the Radiative Transfer Equation (RTE), which in its full form is intractable in real time (Chandrasekhar, 1960). Real-time games historically approximated smoke using billboarded 2D sprites, which do not occlude correctly and cannot interact with scene geometry. Valve's CS2 (2023) introduced a system in which a smoke grenade detonates and a volumetric cloud fills the available space in a room, correctly blocked by walls and doors. The design was publicly analyzed by Gunnell (2023) in a Unity recreation. This project ports that system to standalone OpenGL 4.3/C++, implementing every subsystem from scratch using compute shaders, SSBOs, and 3D textures.

The system is structured as five sequential GPU passes executed each frame:

1. **Voxelization** — convert static scene geometry to a binary voxel occupancy grid (run once at startup)
2. **Flood fill propagation** — expand a smoke density SSBO from the detonation voxel, blocked by occupied voxels
3. **Worley noise generation** — regenerate a 128³ animated noise volume
4. **Volumetric ray marching** — integrate scattered radiance along each camera ray through the density field
5. **Composite** — blend the half-resolution ray march result over the scene using Beer-Lambert transmittance

---

## 2. Static Scene Voxelization

### 2.1 Problem and Method Selection

To propagate smoke realistically, the system must know which regions of space are solid. This requires converting the scene's triangle mesh into a 3D occupancy grid. Three approaches are common in literature:

| Method | Description | Limitation |
|---|---|---|
| Conservative GPU rasterization | Render mesh to 3 orthographic views with conservative rasterization enabled | Misses thin triangles not aligned with any view axis; requires `GL_NV_conservative_raster` |
| Scanline z-parity fill | Cast rays along one axis; toggle solid/empty at each surface crossing | Produces filled interiors — incorrect for open rooms; assumes manifold mesh |
| **Triangle-AABB SAT** | Test each triangle against every voxel it overlaps | Correct for open meshes; parallelises over triangles; no manifold assumption |

The SAT approach is selected as it is robust to open meshes (which rooms typically are, having no ceiling cap) and maps cleanly onto a GPU compute kernel where each thread handles one triangle.

### 2.2 Separating Axis Theorem (SAT)

The SAT states that two convex shapes are disjoint if and only if there exists a *separating axis* — an axis onto which both shapes' projections do not overlap (Gottschalk, Lin & Manocha, 1996). For a triangle-AABB test, exactly **13 potential separating axes** must be checked. Any single axis showing non-overlap proves the shapes are disjoint and the voxel is empty:

**3 cardinal AABB face normals:**

$$\hat{x} = (1,0,0), \quad \hat{y} = (0,1,0), \quad \hat{z} = (0,0,1)$$

**9 edge-cross-product axes** (one per pair of triangle edge × cardinal axis):

$$\mathbf{a}_{ij} = \mathbf{e}_i \times \hat{c}_j, \quad i \in \{0,1,2\}, \; j \in \{x,y,z\}$$

where $\mathbf{e}_0 = v_1 - v_0$, $\mathbf{e}_1 = v_2 - v_1$, $\mathbf{e}_2 = v_0 - v_2$.

**1 triangle face normal:**

$$\mathbf{n} = \mathbf{e}_0 \times \mathbf{e}_1$$

For each axis $\mathbf{a}$, the test checks whether the projected intervals overlap:

$$p_i = \mathbf{a} \cdot v_i, \quad r = h_x|a_x| + h_y|a_y| + h_z|a_z|$$
$$\text{separated} \iff \min(p_0,p_1,p_2) > r \; \text{or} \; \max(p_0,p_1,p_2) < -r$$

where $\mathbf{h}$ is the AABB half-extent and the triangle is translated to the AABB centre (Schwarz & Seidel, 2010).

### 2.3 GPU Implementation

The compute kernel dispatches **one thread per triangle**. Each thread:
1. Computes the triangle's axis-aligned bounding box in grid coordinates
2. Iterates over only the voxels within that AABB (typically a small set)
3. Translates each triangle's vertices to the voxel centre and runs the 13-axis test
4. On intersection: `atomicOr(voxels[idx], 1)` — atomic write is required because multiple triangles may touch the same voxel in parallel

```glsl
// One thread per triangle — inner loop over triangle AABB voxels
ivec3 gMin = ivec3(floor((triMin - u_BoundsMin) / u_VoxelSize));
ivec3 gMax = ivec3(floor((triMax - u_BoundsMin) / u_VoxelSize));

for (int z = gMin.z; z <= gMax.z; z++)
for (int y = gMin.y; y <= gMax.y; y++)
for (int x = gMin.x; x <= gMax.x; x++) {
    vec3 center = u_BoundsMin + (vec3(x,y,z) + 0.5) * u_VoxelSize;
    if (triIntersectsAABB(v0 - center, v1 - center, v2 - center, halfExt))
        atomicOr(voxels[flatIdx(ivec3(x,y,z))], 1);
}
```

This is preferred over Schwarz & Seidel's solid voxelization variant because the smoke system only requires *surface* voxels — the interior is the free space through which smoke propagates.

---

## 3. Smoke Volume Propagation

### 3.1 Representation

The smoke density field is stored as two integer SSBOs (`ping` and `pong`) of size $N_x \times N_y \times N_z$, where each element holds a density value in $[0, V_{max}]$. Integer storage enables `atomicOr` on voxels and avoids floating-point precision issues at dispatch boundaries. Using a ping-pong double-buffer scheme, the propagation shader reads from the `src` SSBO and writes to `dst`, then the buffers are swapped. This avoids GPU read-write hazards that would occur if the same buffer were used for both (Pharr, Jakob & Humphreys, 2023).

### 3.2 Temporal Growth Curve

On detonation, a seed value $V_{seed}(t)$ is stamped onto the seed voxel each propagation step. This value grows from 0 to $V_{max}$ over the fill duration $T_f$ according to a power-law curve:

$$V_{seed}(t) = V_{max} \cdot \left(\frac{t}{T_f}\right)^{\alpha}, \quad \alpha = 0.25$$

The exponent $\alpha < 1$ produces a concave function: the derivative at $t = 0$ is theoretically infinite (explosive expansion), while the approach to $V_{max}$ is very slow. Numerically:

| $t/T_f$ | $V_{seed}/V_{max}$ | Marginal growth remaining |
|---|---|---|
| 0.01 | 56% | 44% |
| 0.10 | 75% | 25% |
| 0.50 | 84% | 16% |
| 0.90 | 97% | 3% |
| 0.95 | 99% | 1% |

This matches the observed CS2 behaviour: the grenade cloud rapidly fills most of its volume within the first second, then spends the remaining time slowly pressing into corners and crevices. A simple quadratic ease-in (`2t²` for `t < 0.5`) was considered but produces a symmetric S-curve that is too slow at the start and too fast at the end for smoke-grenade aesthetics.

### 3.3 Ellipsoid Spatial Constraint

Smoke grenades in CS2 expand in an approximately ellipsoidal volume — wider than it is tall, reflecting buoyancy effects and the grenade's ground-level detonation. An explicit ellipsoidal gate is applied in the fill shader before propagation:

$$\left(\frac{\Delta x}{r_{xz}}\right)^2 + \left(\frac{\Delta y}{r_y}\right)^2 + \left(\frac{\Delta z}{r_{xz}}\right)^2 \leq 1$$

where $(\Delta x, \Delta y, \Delta z)$ is the offset from the seed voxel in voxel coordinates, and the radii at full expansion are:

$$r_{xz} = V_{max} \cdot s_{xz}, \quad r_y = V_{max} \cdot s_y$$

with shape constants $s_{xz} = 1.0$ and $s_y = 0.6$ (producing an oblate spheroid, 40% shorter in the vertical axis). Voxels outside this ellipsoid are immediately set to 0 regardless of their propagated value. This constraint is evaluated in normalised ellipsoid space:

```glsl
vec3 diff = vec3(coord - u_SeedCoord);
float dx = diff.x / (u_MaxSeedVal * u_RadiusXZ);
float dy = diff.y / (u_MaxSeedVal * u_RadiusY);
float dz = diff.z / (u_MaxSeedVal * u_RadiusXZ);
float ellipsoidDist = dx*dx + dy*dy + dz*dz;  // squared normalised distance
if (ellipsoidDist > 1.0) { dst[idx] = 0; return; }
```

An alternative approach of applying anisotropic decay (decrementing Y-neighbours by 2 instead of 1) was tested first. This produces the correct aspect ratio but results in octahedral rather than ellipsoidal iso-surfaces due to the L1 distance metric of 6-connected flood fill. The explicit coordinate-space gate produces geometrically correct ellipsoids.

---

## 4. Obstacle-Aware Flood Fill

### 4.1 L1 vs L2 Distance and Density Mapping

Standard 6-connected BFS flood fill propagates with **L1 (Manhattan) distance** from the seed — the iso-surface of equal density at a given step count is an octahedron, not a sphere. This produces a characteristic diamond/pyramidal appearance that is visually incorrect for smoke.

To produce smooth spherical iso-surfaces while retaining the wall-blocking behaviour of BFS, the density stored in each voxel is decoupled from the hop count and mapped instead to the **L2 (Euclidean) normalised ellipsoid distance**:

$$d(v) = V_{max}(t) \cdot \Bigl(1 - \sqrt{e_v}\Bigr), \quad e_v = \left(\frac{\Delta x}{r_{xz}}\right)^2 + \left(\frac{\Delta y}{r_y}\right)^2 + \left(\frac{\Delta z}{r_{xz}}\right)^2$$

The BFS hop-count still controls **reachability** (whether `maxVal > 0` after neighbour sampling) while $d(v)$ determines the **rendered density**. This separates two concerns:

- The flood fill wavefront controls *which* voxels are filled (walls still block naturally)
- The Euclidean function determines *how dense* each filled voxel appears (spherical iso-surfaces)

```glsl
if (maxVal <= 0) {
    dst[idx] = 0;
} else {
    float edist = sqrt(ellipsoidDist);          // linear 0..1
    dst[idx] = max(int(float(u_MaxSeedVal) * (1.0 - edist)), 1);
}
```

### 4.2 Wall-Blocking Propagation

The propagation rule for each air voxel is:

$$V_{dst}(v) = \max\left(0,\; \max_{u \in \mathcal{N}(v),\; \text{walls}[u]=0} V_{src}(u) - 1\right)$$

where $\mathcal{N}(v)$ is the 6-connected neighbourhood of $v$. Wall voxels (`walls[u] != 0`) are excluded from the max, so they act as absorbing barriers — the only path for smoke to propagate around a wall is through the air voxels adjacent to its edges. This produces the desired behaviour: a voxel behind a wall can only be reached via paths that go around the wall, and those paths are longer, resulting in lower density on the far side of an obstacle.

**Why 6-connectivity over 26-connectivity:** Using all 26 neighbours (face + edge + corner adjacencies) would allow smoke to "tunnel" diagonally through a wall one voxel thick by passing through the corner junction between two wall voxels. 6-connectivity ensures a single-voxel-thick wall is always an impenetrable barrier.

### 4.3 Ping-Pong Double Buffering

The propagation shader reads from `src` (binding 1) and writes to `dst` (binding 2). After dispatch, a memory barrier is inserted and the bindings are swapped:

```cpp
glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
pingIsSrc = !pingIsSrc;  // swap src/dst each step
```

Without double-buffering, a voxel that has been updated in the same dispatch pass might propagate its new (not original) value to its neighbours within the same step, creating a dependency on dispatch order and causing non-deterministic propagation fronts (Pharr et al., 2023).

---

## 5. Procedural Smoke Animation: Worley Noise

### 5.1 Cellular Noise Theory

Static density fields produce visually inert smoke. Real smoke exhibits turbulent micro-structure: swirling filaments and billowing lobes. This is approximated using **Worley cellular noise** (Worley, 1996), which at each sample position $\mathbf{p}$ computes the Euclidean distance to the nearest randomly-placed *feature point* within a tiled grid of cells:

$$F_1(\mathbf{p}) = \min_{i} \|\mathbf{p} - \mathbf{f}_i\|_2$$

where $\mathbf{f}_i$ is a feature point within cell $i$. Inverting this and applying a power function sharpens the internal clouds:

$$w(\mathbf{p}) = \left(1 - F_1(\mathbf{p})\right)^6$$

The exponent 6 concentrates the bright values near feature points and produces the characteristic cellular cloud puffs. Alternative choices: Perlin noise (gradient-based, smoother but less cloud-like at large scales), Value noise (blocky, inappropriate for smoke). Worley noise is preferred for smoke specifically because its cell-centred bright regions naturally resemble convective cloud columns (Schneider & Vines, 2015).

### 5.2 Tiling and GPU Hash

To tile the noise volume (so it can repeat across large scenes), cell coordinates are wrapped with modulo before the hash lookup:

```glsl
cell = ((cell % wrap) + wrap) % wrap;
int n = cell.x + cell.y * 137 + cell.z * 7919;
```

The double modulo handles negative coordinates. The **Hugo Elias integer hash** maps a cell index to a pseudorandom float in $[0, 1]$ with good avalanche properties (small changes in input produce large changes in output):

$$n \leftarrow n \oplus (n \ll 13), \quad n \leftarrow n \cdot (n^2 \cdot 15731 + 789221) + 1376312589$$
$$h = \frac{n \;\&\; 0x7\text{FFFFFFF}}{0x7\text{FFFFFFF}}$$

### 5.3 Fractional Brownian Motion (fBm)

A single octave of Worley noise produces smooth, large blobs. Turbulent detail at multiple scales is achieved by summing $K$ octaves with geometrically increasing frequency (lacunarity $= 2$) and geometrically decreasing amplitude (persistence $= 0.5$):

$$\text{fBm}(\mathbf{p}) = \sum_{k=0}^{K-1} \frac{1}{2^k} \cdot w\!\left(2^k \mathbf{p} + \boldsymbol{\delta}_k\right)$$

where $\boldsymbol{\delta}_k = k \cdot \mathbf{u}(t)$ is a per-octave domain warp offset animated over time $t$ at speed $u$. The varying offset across octaves produces swirling motion at each scale — a computationally cheap approximation to velocity-field-driven advection. The volume is regenerated every frame by an 8×8×8 compute shader writing to a `GL_R16F` 128³ 3D texture.

The final smoke density at a ray march sample point $\mathbf{p}$ is:

$$\rho(\mathbf{p}) = V(\mathbf{p}) \cdot \text{fBm}(\mathbf{p})$$

where $V(\mathbf{p})$ is the flood-fill density value sampled from the SSBO via trilinear interpolation.

---

## 6. Volumetric Rendering

### 6.1 Physical Model

Smoke is an *optically thin participating medium*: light passing through it is both absorbed and scattered. The governing equation along a ray $\mathbf{r}(t) = \mathbf{o} + t\hat{\mathbf{d}}$ is the simplified single-scattering RTE (Max, 1995):

$$L(\mathbf{o}, \hat{\mathbf{d}}) = \int_{t_{min}}^{t_{max}} \sigma_s(\mathbf{r}(t)) \cdot p(\hat{\mathbf{d}}, \hat{\mathbf{l}}) \cdot L_\ell(\mathbf{r}(t)) \cdot T(\mathbf{o}, \mathbf{r}(t)) \, dt$$

where $\sigma_s$ is the scattering coefficient, $p$ is the phase function, $L_\ell$ is the light radiance (modulated by shadow transmittance), and $T$ is the transmittance from camera to sample point.

### 6.2 Beer-Lambert Transmittance

The transmittance of a homogeneous slab of thickness $\Delta s$ with extinction coefficient $\sigma_e = \sigma_a + \sigma_s$ is given by the Beer-Lambert law (Kajiya & Von Herzen, 1984):

$$T(\Delta s) = e^{-\sigma_e \cdot \Delta s}$$

In the accumulation loop, the running transmittance $\hat{T}$ is multiplied at each step:

$$\hat{T} \leftarrow \hat{T} \cdot e^{-\rho(\mathbf{r}(t)) \cdot \sigma_e \cdot \Delta s}$$

Early termination when $\hat{T} < \epsilon$ (typically 0.01) avoids wasted marching in fully-opaque regions, providing a significant performance saving in practice (Wrenninge, 2012).

The scattered light contribution accumulated at each step is:

$$\Delta L = L_\ell \cdot T_{shadow} \cdot \hat{T} \cdot p(\cos\theta) \cdot \sigma_s \cdot \rho \cdot \Delta s$$

where $T_{shadow}$ is the transmittance of a secondary shadow ray marched from the sample point toward the light source.

### 6.3 Powder Effect (Fake Multiple Scattering)

Single-scattering Beer-Lambert underestimates the perceived opacity of thick smoke because it ignores multiple scattering paths. A common real-time approximation known as the *powder effect* (Schneider & Vines, 2015) adds a view-dependent darkening term that mimics the way densely-packed particles absorb more light than the simple single-scatter model predicts:

$$P_{powder} = 1 - e^{-2\,\rho \cdot \sigma_e \cdot \Delta s}$$

This value is used to modulate the light contribution, making the interior of the cloud appear darker than its outer surface — the characteristic "cotton-ball" appearance of real smoke. Physically, it is a first-order approximation to the multiple-scattering integral that is otherwise too expensive to evaluate in real time.

### 6.4 Henyey-Greenstein Phase Function

Isotropic phase functions ($p = 1/4\pi$) produce smoke that scatters light equally in all directions, which is physically inaccurate. Real smoke particles preferentially forward-scatter (Mie scattering regime). The Henyey-Greenstein phase function (Henyey & Greenstein, 1941) provides a single-parameter model:

$$p_{HG}(\cos\theta, g) = \frac{1}{4\pi} \cdot \frac{1 - g^2}{\left(1 + g^2 - 2g\cos\theta\right)^{3/2}}$$

where $g \in [-1, 1]$ is the *asymmetry parameter*: $g = 0$ gives isotropic scattering, $g > 0$ forward-scatters (smoke appears brighter looking toward the light), $g < 0$ back-scatters. Real smoke in game context uses $g \approx 0.3$–$0.6$ (Gunnell, 2023; Wrenninge, 2012).

The phase function is evaluated at each ray march step using $\cos\theta = \hat{\mathbf{d}} \cdot \hat{\mathbf{l}}$ (dot product of ray and light directions).

### 6.5 Coarse-Fine Two-Phase March

Marching at uniform step size through mostly-empty space is wasteful. A two-phase strategy is used:

1. **Coarse skip phase:** March at $2 \times$ voxel size until the trilinearly-interpolated density exceeds a small threshold (e.g., 0.005). This skips empty space cheaply.
2. **Fine accumulation phase:** Switch to a small fixed step size $\Delta s$ and apply Beer-Lambert + phase accumulation.

The ray is clipped to the scene geometry depth buffer (reconstructed from a depth-only FBO rendered before the compute dispatch), so smoke does not bleed through walls when viewed from close range.

---

## 7. Summary

The table below maps each technique to its primary reference and the design rationale over alternatives:

| Subsystem | Method | Key Reference | Why Not Alternative |
|---|---|---|---|
| Voxelization | 13-axis SAT, GPU per-triangle | Schwarz & Seidel (2010) | Conservative rasterization requires extension; z-parity fill requires closed mesh |
| Temporal expansion | Power-law ease $t^{0.25}$ | — | Quadratic ease-in too symmetric; step function unrealistic |
| Spatial constraint | Oblate ellipsoid gate in normalised coords | Gunnell (2023) | Pure anisotropic decay produces octahedral L1 shape |
| Density iso-surfaces | Euclidean distance in ellipsoid space | — | Hop-count (L1) produces pyramid cross-section |
| Flood fill blocking | 6-connected BFS with wall mask | — | 26-connected allows diagonal tunnelling through 1-voxel walls |
| Noise | Worley fBm + domain warp | Worley (1996) | Perlin noise too smooth; Value noise too blocky for convective clouds |
| Transmittance | Beer-Lambert exponential | Kajiya & Von Herzen (1984), Max (1995) | Linear absorption incorrect for thick media |
| Phase function | Henyey-Greenstein, $g \approx 0.4$ | Henyey & Greenstein (1941) | Isotropic underestimates forward-scatter lobe visible at sunrise/sunset |
| Self-shadowing | Shadow ray + powder effect | Schneider & Vines (2015) | Full multiple-scattering too expensive; Lambert-only too flat |

---

## References

Chandrasekhar, S. (1960). *Radiative transfer*. Dover Publications.

Gottschalk, S., Lin, M. C., & Manocha, D. (1996). OBBTree: A hierarchical structure for rapid interference detection. *Proceedings of the 23rd Annual Conference on Computer Graphics and Interactive Techniques (SIGGRAPH '96)*, 171–180. https://doi.org/10.1145/237170.237244

Gunnell, G. (2023). *CS2 smoke grenades* [Open-source Unity recreation and breakdown video]. GitHub. https://github.com/GarrettGunnell/CS2-Smoke-Grenades

Henyey, L. G., & Greenstein, J. L. (1941). Diffuse radiation in the galaxy. *The Astrophysical Journal, 93*, 70–83. https://doi.org/10.1086/144246

Kajiya, J. T., & Von Herzen, B. P. (1984). Ray tracing volume densities. *ACM SIGGRAPH Computer Graphics, 18*(3), 165–174. https://doi.org/10.1145/964965.808594

Max, N. (1995). Optical models for direct volume rendering. *IEEE Transactions on Visualization and Computer Graphics, 1*(2), 99–108. https://doi.org/10.1109/2945.468400

Pharr, M., Jakob, W., & Humphreys, G. (2023). *Physically based rendering: From theory to implementation* (4th ed.). MIT Press. https://www.pbr-book.org

Schneider, J., & Vines, N. (2015). Real-time volumetric cloudscapes. In W. Engel (Ed.), *GPU Pro 7: Advanced Rendering Techniques* (pp. 97–127). CRC Press.

Schwarz, M., & Seidel, H.-P. (2010). Fast parallel surface and solid voxelization on GPUs. *ACM Transactions on Graphics (SIGGRAPH Asia), 29*(6), Article 179. https://doi.org/10.1145/1882261.1866201

Worley, S. (1996). A cellular texture basis function. *Proceedings of the 23rd Annual Conference on Computer Graphics and Interactive Techniques (SIGGRAPH '96)*, 291–294. https://doi.org/10.1145/237170.237267

Wrenninge, M. (2012). *Production volume rendering: Design and implementation*. CRC Press.

# CS2 Volumetric Smoke Grenade — Implementation Report

**Course:** 50.017 Graphics and Visualisation
**Project:** Real-Time Volumetric Smoke using OpenGL 4.3 Compute Shaders

---

## Abstract

This report describes the GPU implementation of a real-time volumetric smoke grenade system in OpenGL 4.3/C++, reproducing the technique introduced by Valve in *Counter-Strike 2* (2023). The system is structured into four subsystems: (1) a static scene voxelizer that converts triangle meshes into a binary occupancy grid using the Separating Axis Theorem; (2) a smoke volume modeller that propagates a density field outward from a detonation point using a decay-based flood fill, blocked by scene geometry; (3) a volumetric renderer that marches camera rays through the density field using Beer-Lambert transmittance, Perlin-Worley noise erosion, and Henyey-Greenstein/Rayleigh phase functions; and (4) a fluid dynamics solver that applies incompressible Navier-Stokes pressure projection, temperature-driven buoyancy, baroclinic torque, and semi-Lagrangian advection to produce physically-motivated smoke motion. All subsystems run as GPU compute shaders operating on shared SSBO and 3D texture resources with no CPU readback during the render loop.

---

## 1. Modelling the Arena

### 1.1 Problem and Approach

For smoke to behave realistically — filling rooms, pooling in corners, and being blocked by walls — the renderer must know which regions of the scene are solid. This requires converting the arena's triangle mesh into a 3D binary occupancy grid. Three methods are common in literature:

| Method | Description | Limitation |
|---|---|---|
| Conservative GPU rasterization | Render mesh to 3 orthographic views with conservative rasterization enabled | Misses thin triangles not aligned with any view axis; requires `GL_NV_conservative_raster` extension |
| Scanline z-parity fill | Cast rays along one axis; toggle solid/empty at each surface crossing | Fills interiors — incorrect for open rooms; requires a closed (manifold) mesh |
| **Triangle-AABB SAT** | Test each triangle against every voxel it overlaps | Correct for open meshes; parallelises trivially over triangles; no manifold assumption |

The SAT approach is selected because arena geometry is typically open (rooms have no ceiling cap, doorways are open arches) and the system only requires *surface* voxels rather than filled interiors — the interior air volume is exactly the space through which smoke propagates.

### 1.2 Separating Axis Theorem (SAT)

The SAT states that two convex shapes are disjoint if and only if there exists a *separating axis* — an axis onto which both shapes' projected intervals do not overlap (Gottschalk, Lin & Manocha, 1996). For a triangle-AABB pair, exactly **13 candidate axes** must be tested. If any single axis shows separation, the shapes are disjoint and the voxel is empty:

**3 AABB face normals (cardinal axes):**

$$\hat{x} = (1,0,0), \quad \hat{y} = (0,1,0), \quad \hat{z} = (0,0,1)$$

**9 edge-cross-product axes** (one per pair of triangle edge x cardinal axis):

$$\mathbf{a}_{ij} = \mathbf{e}_i \times \hat{c}_j, \quad i \in \{0,1,2\},\; j \in \{x,y,z\}$$

where the triangle edges are $\mathbf{e}_0 = v_1 - v_0$, $\mathbf{e}_1 = v_2 - v_1$, $\mathbf{e}_2 = v_0 - v_2$.

**1 triangle face normal:**

$$\mathbf{n} = \mathbf{e}_0 \times \mathbf{e}_1$$

For each axis $\mathbf{a}$, separation is detected by projecting both shapes and checking for a gap. The triangle is translated so the AABB is centred at the origin, then the test becomes:

$$p_i = \mathbf{a} \cdot v_i, \quad r = h_x|a_x| + h_y|a_y| + h_z|a_z|$$
$$\text{separated} \iff \min(p_0, p_1, p_2) > r \;\text{or}\; \max(p_0, p_1, p_2) < -r$$

where $\mathbf{h}$ is the AABB half-extent (Schwarz & Seidel, 2010). A voxel is marked occupied (`atomicOr(voxels[idx], 1)`) only if all 13 axes show overlap.

### 1.3 GPU Compute Implementation

The compute kernel dispatches **one thread per triangle**. Each thread:
1. Computes the triangle's axis-aligned bounding box in grid coordinates
2. Iterates over only the voxels that fall within that AABB (a small local set)
3. Translates the triangle's vertices to each voxel's centre and runs the 13-axis test
4. On intersection: writes atomically to prevent data races when multiple triangles share a voxel

```glsl
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

The voxel grid supports two wall types: **opaque solid** (value 1, for physical walls and floors) and **invisible barrier** (value 2, for the arena perimeter boundary that blocks smoke without being rendered). This allows the arena to have a clean visual boundary without needing visible geometry at every edge.

### 1.4 Arena Grid Parameters

The arena is initialised at voxel size `0.15` world units with a `96 × 32 × 96` grid. These can be adjusted at runtime via the **Arena** panel in the debug GUI — changes only take effect when the "Rebuild Arena" button is clicked, since rebuilding destroys and reinitialises all GPU buffers and is too expensive to run continuously from a slider.

| Parameter | Default | Effect of increasing | Effect of decreasing |
|---|---|---|---|
| Voxel size | `0.15` | Coarser geometry representation, lower memory, faster flood fill and fluid solve | Finer walls and smoke boundaries, higher memory, slower simulation |
| Grid X/Z | `96` | Larger horizontal arena footprint | Smaller playable area |
| Grid Y | `32` | Taller arena, more vertical smoke travel room | Shallower arena; smoke hits ceiling sooner |

Larger grids are limited by GPU memory (each grid cell holds four SSBOs: walls, density, velocity as `vec4`, and pressure) and compute dispatch cost (all fluid solvers scale as $O(N_x N_y N_z)$).

---

## 2. Modelling the Smoke using Voxels

### 2.1 Representation and Role of the Flood Fill

The smoke volume is represented as a floating-point density SSBO of size $N_x \times N_y \times N_z$. Rather than injecting density directly into the fluid solver, a separate **flood fill** system acts as the density source. The flood fill has two responsibilities:

1. **Wall-aware boundary propagation** — it expands outward from the detonation point using BFS, naturally stopping at wall voxels. This guarantees that smoke can never appear on the far side of a wall regardless of what the fluid solver does.
2. **Fallback source term** — when semi-Lagrangian advection is disabled (toggled off in the debug GUI), the flood fill continues to inject density directly, so the smoke system remains functional without the fluid solver.

The fluid solver then operates on top of this injected density, adding buoyancy, pressure-driven flow, and turbulent advection. The two systems are complementary: the flood fill provides geometric correctness and a continuous density source; the fluid solver provides physical realism and visual richness.

### 2.2 Temporal Growth Curve

On detonation, a seed budget $B(t)$ is stamped onto the detonation voxel each propagation step, growing from 0 to $B_{max}$ over the fill duration $T_f$ according to a **cubic ease-out** curve:

$$B(t) = B_{max} \cdot \left(1 - \left(1 - \frac{t}{T_f}\right)^3\right)$$

This function has a steep initial slope (rapid early expansion filling the bulk of the volume quickly) that flattens as $t \to T_f$ (slow approach to the final boundary). This matches observed CS2 behaviour: the grenade cloud fills most of its volume within the first second, then spends the remaining time slowly pressing into corners and crevices. A simple linear ramp or quadratic ease would fill corners at the wrong rate relative to the initial burst.

The maximum budget $B_{max}$ is set to the L2 distance from the detonation voxel to the furthest point of the target ellipsoid, scaled by a `wallDetourFactor` that accounts for extra path length smoke must travel around walls:

$$B_{max} = \sqrt{2r_{xz}^2 + r_y^2} \cdot B_{seed} \cdot k_{detour}$$

where $r_{xz}$ and $r_y$ are the ellipsoid radii in voxel units and $k_{detour} \geq 1$ gives the BFS wavefront enough budget to route around obstacles without the smoke dying in front of them.

### 2.3 Ellipsoid Spatial Constraint

Smoke grenades expand in an approximately oblate spheroidal volume — wider than tall — to match the characteristic visual shape of CS2 smoke following a ground-level detonation. An explicit ellipsoidal gate is applied in the fill shader: any voxel outside the ellipsoid is zeroed immediately regardless of its propagated budget:

$$\left(\frac{\Delta x}{r_{xz}}\right)^2 + \left(\frac{\Delta y}{r_y}\right)^2 + \left(\frac{\Delta z}{r_{xz}}\right)^2 \leq 1$$

where $(\Delta x, \Delta y, \Delta z)$ is the offset from the detonation voxel. In normalised ellipsoid coordinates:

```glsl
float ellipsoidDist = dx*dx + dy*dy + dz*dz;
if (ellipsoidDist > 1.0) { dst[idx] = 0; return; }
```

An alternative of applying anisotropic decay (decrementing Y-neighbours by a larger step) was tested first but produces octahedral rather than ellipsoidal iso-surfaces due to the L1 distance metric of 6-connected flood fill. The explicit coordinate-space gate produces geometrically correct ellipsoids.

### 2.4 L1 Reachability vs L2 Density

Standard 6-connected BFS propagates with **L1 (Manhattan) distance** — the iso-surface of equal hop-count is an octahedron, not a sphere. The two concerns are therefore decoupled:

- **BFS hop-count** controls *reachability* — whether the wavefront reaches a voxel at all (walls still block naturally)
- **Euclidean distance** determines *rendered density* — the density value assigned once the voxel is reached

The density assigned to a reached voxel is mapped to its normalised ellipsoid distance:

$$d(v) = B_{max}(t) \cdot \left(1 - \sqrt{e_v}\right), \quad e_v = \left(\frac{\Delta x}{r_{xz}}\right)^2 + \left(\frac{\Delta y}{r_y}\right)^2 + \left(\frac{\Delta z}{r_{xz}}\right)^2$$

This produces smooth spherical density iso-surfaces that are still correctly blocked by walls.

### 2.5 Wall-Blocking Propagation

The propagation rule for each air voxel is:

$$V_{dst}(v) = \max\left(0,\; \max_{u \in \mathcal{N}(v),\;\text{walls}[u]=0} V_{src}(u) - 1\right)$$

where $\mathcal{N}(v)$ is the 6-connected face-adjacent neighbourhood. Wall voxels are excluded from the max, so they act as absorbing barriers. Smoke can only reach a voxel behind a wall by routing through the air gap at the wall's edges — a longer path — which naturally results in lower budget and therefore lower density on the far side.

**Why 6-connectivity over 26-connectivity:** Using all 26 neighbours would allow smoke to tunnel diagonally through a wall one voxel thick, because the diagonal path passes through the shared corner between two wall voxels. 6-connectivity guarantees that a single-voxel-thick wall is always an impenetrable barrier.

### 2.6 Ping-Pong Double Buffering

The propagation shader reads from a `src` SSBO and writes to a `dst` SSBO. After each dispatch, a memory barrier is issued and the buffers are swapped:

```cpp
glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
pingIsSrc = !pingIsSrc;
```

Without double-buffering, a voxel updated earlier in the same dispatch might propagate its new value to its neighbours within the same step, creating a dispatch-order dependency and non-deterministic wavefront propagation.

### 2.7 Flood Fill Parameters

| Parameter | Default | Effect of increasing | Effect of decreasing |
|---|---|---|---|
| Flood fill steps per frame | `1` | Faster spatial expansion — smoke fills the room quicker each frame | Slower expansion; smoke creeps outward more gradually |
| Smoke density inject strength | `0.8` | Higher density source; smoke appears more opaque near the detonation point | Lower source density; smoke fades faster away from centre |
| Velocity inject strength | `0.1` | Stronger initial blast wave; smoke is thrown outward more forcefully in the first 2.5 s | Gentler expansion; smoke drifts rather than explodes outward |
| Temperature inject strength | `30.0` | More heat injected; stronger buoyant rise and more baroclinic vorticity at boundaries | Less heat; smoke rises weakly and behaves more like a neutral-density gas |

The **flood fill steps per frame** was reduced from 3 to 1 in the latest revision to produce a slower, more visually believable expansion speed. At 3 steps per frame the smoke was reaching room boundaries noticeably faster than CS2 reference footage.

The **temperature inject strength** of 30.0 is a dimensionless scale chosen to produce a visible buoyant column rise within 1–2 seconds of detonation. It represents the initial "heat" of the grenade combustion; as this temperature dissipates (cooling rate 0.01 per frame), the buoyant rise naturally slows and the smoke settles.

---

## 3. Rendering of the Smoke

### 3.1 Perlin-Worley Noise

Static density fields produce visually inert smoke. Real smoke exhibits turbulent micro-structure: billowing lobes, wispy filaments, and internal puffiness. This is approximated using a **Perlin-Worley blend**, which combines the large-scale gradient flow of Perlin noise with the convective cell structure of Worley cellular noise (Worley, 1996).

**Worley noise** at a sample position $\mathbf{p}$ computes the distance to the nearest randomly-placed *feature point* within a tiled cell grid:

$$F_1(\mathbf{p}) = \min_i \|\mathbf{p} - \mathbf{f}_i\|_2$$

Inverting and applying a cubic smoothing produces bright centres with soft falloff:

$$w(\mathbf{p}) = \left(1 - F_1(\mathbf{p})\right)^3$$

**Perlin noise** at the same position produces a smooth gradient-based value $p(\mathbf{p}) \in [0, 1]$ using a standard quintic-fade lattice construction.

**The blend** uses Perlin as a brightness modulator on Worley:

$$\text{pw}(\mathbf{p}) = \text{clamp}\!\left(w(\mathbf{p}) \cdot \bigl(0.4 + p(\mathbf{p})\bigr),\; 0,\; 1\right)$$

The constant 0.4 ensures that even at low Perlin values the Worley cell structure remains partially visible — Perlin never fully blacks out the cellular pattern. At high Perlin values (approaching 1), the combined value brightens the Worley clusters without oversaturating. Pure Worley noise produces uniform cell blobs that lack large-scale variation; pure Perlin noise is too smooth to resemble convective cloud columns. The blend produces large-scale flow (Perlin) within locally cellular structure (Worley), which reads visually as turbulent smoke puffs.

### 3.2 Fractional Brownian Motion (fBm)

A single octave of Perlin-Worley noise produces large, smooth features. Turbulent detail at multiple scales is achieved by summing 3 octaves with geometrically increasing frequency (lacunarity $= 2$) and geometrically decreasing amplitude (persistence $= 0.5$):

$$\text{fBm}(\mathbf{p}) = \sum_{k=0}^{2} \frac{1}{2^k} \cdot \text{pw}\!\left(2^k \mathbf{p},\, 2^k \cdot c_0\right)$$

Each octave is animated at a distinct speed ($t \cdot 0.0025$, $t \cdot 0.0055$, $t \cdot 0.0110$), so different scales drift at different rates, approximating a turbulence cascade without full velocity-field integration. The noise volume is regenerated every frame by an $8 \times 8 \times 8$ compute shader dispatched over a $128^3$ `GL_R16F` 3D texture.

The hash function for cell feature points uses the Hugo Elias integer hash:

$$n \leftarrow n \oplus (n \ll 13), \quad n \leftarrow n \cdot (n^2 \cdot 15731 + 789221) + 1376312589$$
$$h = \frac{n \;\&\; \texttt{0x7FFFFFFF}}{\texttt{0x7FFFFFFF}}$$

### 3.3 Physical Model: Radiative Transfer

Smoke is an *optically thin participating medium*: light passing through it is both absorbed and scattered. The governing equation along a ray $\mathbf{r}(t) = \mathbf{o} + t\hat{\mathbf{d}}$ is the simplified single-scattering Radiative Transfer Equation (Max, 1995):

$$L(\mathbf{o}, \hat{\mathbf{d}}) = \int_{t_{\min}}^{t_{\max}} \sigma_s\!\left(\mathbf{r}(t)\right) \cdot p(\hat{\mathbf{d}}, \hat{\mathbf{l}}) \cdot L_\ell\!\left(\mathbf{r}(t)\right) \cdot T\!\left(\mathbf{o}, \mathbf{r}(t)\right) dt$$

where $\sigma_s$ is the scattering coefficient, $p$ is the phase function, $L_\ell$ is the radiance from the light, and $T$ is the transmittance from camera to sample.

### 3.4 Beer-Lambert Transmittance

The transmittance of a homogeneous slab of thickness $\Delta s$ with extinction coefficient $\sigma_e = \sigma_a + \sigma_s$ is given by the Beer-Lambert law (Kajiya & Von Herzen, 1984):

$$T(\Delta s) = e^{-\sigma_e \cdot \Delta s}$$

The running transmittance $\hat{T}$ is multiplied at each march step:

$$\hat{T} \leftarrow \hat{T} \cdot e^{-\rho(\mathbf{r}(t)) \cdot \sigma_e \cdot \Delta s}$$

The scattered light accumulated at each step is:

$$\Delta L = L_\ell \cdot T_{shadow} \cdot \hat{T} \cdot p(\cos\theta) \cdot \sigma_s \cdot \rho \cdot \Delta s$$

where $T_{shadow}$ is the transmittance of a 16-step shadow ray marched toward the light source. Early termination at $\hat{T} < 0.01$ avoids wasted computation in fully-opaque regions (Wrenninge, 2012).

### 3.5 Powder Effect (Fake Multiple Scattering)

Single-scattering Beer-Lambert underestimates the perceived opacity of thick smoke because it ignores multiply-scattered paths. The *powder effect* (Schneider & Vines, 2015) adds a view-dependent darkening term that mimics the way densely-packed particles absorb more light than the single-scatter model predicts:

$$P_{powder} = 1 - e^{-2\,\rho \cdot \sigma_e \cdot \Delta s}$$

This makes the interior of the cloud appear darker than its outer surface — the characteristic cotton-ball appearance of real smoke — at the cost of a single additional `exp` call per step.

### 3.6 Phase Functions: Henyey-Greenstein and Rayleigh Blend

**Henyey-Greenstein (HG)** is a single-parameter model for Mie-regime particles (Henyey & Greenstein, 1941):

$$p_{HG}(\cos\theta, g) = \frac{1}{4\pi} \cdot \frac{1 - g^2}{\left(1 + g^2 - 2g\cos\theta\right)^{3/2}}$$

where $g \in [-1, 1]$ is the asymmetry parameter: $g = 0$ gives isotropic scattering, $g > 0$ forward-scatters (smoke looks brighter when the camera looks toward the light), $g < 0$ back-scatters. The default $g = 0.5$ produces a visible forward-scatter lobe that makes smoke brighten when backlit, matching the CS2 reference appearance.

**Rayleigh** is a symmetric two-lobe model for molecular scattering:

$$p_{R}(\cos\theta) = \frac{3}{16\pi}\left(1 + \cos^2\theta\right)$$

The two models are blended continuously via a `phaseBlend` uniform:

$$p(\cos\theta) = (1 - \alpha)\, p_{HG}(\cos\theta, g) + \alpha\, p_{R}(\cos\theta)$$

The result is rescaled by $4\pi$ before use in the accumulation integral so that brightness remains consistent as the blend changes (both functions integrate to 1 over the sphere).

### 3.7 Domain Warp and Worley Erosion

**Domain warp** displaces the sample position by a warp vector derived from the noise texture at three staggered offsets, approximating a curl-like displacement:

$$\mathbf{w} = \begin{pmatrix} W(\mathbf{u}) \\ W(\mathbf{u} + \boldsymbol{\delta}_1) \\ W(\mathbf{u} + \boldsymbol{\delta}_2) \end{pmatrix} \cdot 2 - 1$$

where $\mathbf{u} = \texttt{worldToVolumeUVW}(\mathbf{p}) + t \cdot 0.04$ is time-animated and $\boldsymbol{\delta}_1 = (0.37, 0.11, 0.23)$, $\boldsymbol{\delta}_2 = (0.19, 0.41, 0.07)$. The warp is masked by a density-based smoothstep so only non-trivially dense voxels are displaced, preventing spurious density appearing outside the flood-fill boundary.

**Worley erosion** carves internal structure through a two-stage remapping. Coarse and fine FBM octaves are combined and a detail erosion applied:

$$\text{fbm}_{shaped} = \text{clamp}\!\left(\frac{\text{fbm}_{coarse} - 0.2 \cdot \text{fbm}_{fine}}{1 - 0.2 \cdot \text{fbm}_{fine}},\; 0,\; 1\right)$$

Then a power-curve and haze floor shape the final puffiness:

$$\text{fbm}_{final} = \text{fbm}_{shaped}^{e_p} \cdot (1 - H) + H$$

where $e_p$ is the puff exponent (scaled by `noiseStrength`) and $H$ is the haze floor (`hazeFloor`).

### 3.8 Coarse-Fine Two-Phase Ray March

A two-phase strategy avoids wasting compute in empty space:

1. **Coarse skip phase:** March at $2 \times$ voxel size until density exceeds 0.002.
2. **Fine accumulation phase:** Switch to $0.5 \times$ voxel size and apply Beer-Lambert + phase + shadow + noise.

The ray is clipped to the scene depth buffer (from a depth-only FBO rendered before the compute dispatch) so smoke does not bleed through walls when viewed from close range.

### 3.9 Rendering Parameters

| Parameter | Default | Effect of increasing | Effect of decreasing |
|---|---|---|---|
| `densityScale` | `30.0` | Smoke appears more opaque overall; thinner wisps become visible | Smoke becomes more transparent; interior detail disappears into thin haze |
| `sigmaS` (scattering) | `0.5` | More light scattered toward camera; brighter, whiter smoke | Darker smoke with less internal glow |
| `sigmaA` (absorption) | `0.8` | Smoke absorbs more light; appears darker and more opaque from behind | More light passes through; smoke looks translucent |
| `g` (HG asymmetry) | `0.5` | Stronger forward-scatter lobe; smoke looks much brighter when camera faces light | Approaches isotropic; smoke brightness no longer depends on view direction |
| `phaseBlend` | `0.5` | Shifts toward Rayleigh (symmetric lobes, more even brightness) | Shifts toward pure HG (stronger directional forward scatter) |
| `noiseScale` | `1.3` | Higher noise frequency; smaller, more fragmented puffs | Larger, smoother puffs; smoke looks more like a single mass |
| `noiseStrength` | `0.85` | More aggressive erosion; deep holes carved into the smoke volume | Smoother smoke with less internal breakup |
| `hazeFloor` | `0.1` | Minimum density floor; prevents noise from fully eroding thin wisps to zero | At 0.0, noise can cut all the way through to empty space at boundaries |
| `curlStrength` | `1.0` | Stronger domain warp; more swirling displacement at boundaries | Less warping; smoke boundary stays closer to the flood-fill ellipsoid |

**`densityScale = 30.0`** is the most impactful single parameter. It multiplies the raw density value (which lives in $[0, 1]$ after normalisation) before it enters the Beer-Lambert integral. At 30.0 a voxel at full density produces $e^{-\sigma_e \cdot 30 \cdot \Delta s}$ transmittance per step, which drops to near zero quickly — the smoke is opaque enough to block the scene behind it. Lowering it toward 5–10 produces a thin wispy haze; raising it above 50 makes even dilute smoke appear as a solid block.

**`sigmaS = 0.5` and `sigmaA = 0.8`** were tuned together. A higher absorption than scattering ratio ($\sigma_a > \sigma_s$) means the smoke absorbs more than it reflects — producing the dark grey appearance of CS2 smoke rather than a bright white cloud. Increasing `sigmaS` without increasing `sigmaA` would produce unrealistically bright, milky smoke.

**`noiseScale = 1.3`** was reduced from the previous default of 3.2. At 3.2 the noise frequency was high enough to produce a visibly "lumpy" texture that looked more like a textured sphere than smoke. At 1.3 the puffs are large enough to read as volumetric cloud lobes.

**`hazeFloor = 0.1`** was raised from 0.0. At 0.0 the noise erosion could fully hollow out the boundary of the smoke, producing sharp transparent gaps that looked like the smoke was made of swiss cheese rather than a continuous cloud. The floor of 0.1 ensures there is always a minimum haze density at the boundary, smoothing the transition.

---

## 4. Fluid Dynamics of the Smoke

### 4.1 Role and Motivation

The flood fill system provides a wall-correct density source, but the resulting motion is purely radial and lacks physical realism. A simplified incompressible Navier-Stokes solver running on the same voxel grid provides buoyant rise, pressure-driven wall deflection, and turbulent mixing. The velocity field is packed as a `vec4` where `.xyz` stores the 3D velocity and `.w` stores the local **temperature** — both are advected together using the same semi-Lagrangian backtrace, which ensures temperature is transported with the smoke rather than diffusing independently.

### 4.2 Incompressibility and Pressure Projection

The continuity constraint for incompressible flow requires:

$$\nabla \cdot \mathbf{V} = 0$$

This is enforced each frame via pressure projection (Stam, 1999; Bridson, 2008):

**Step 1 — Compute divergence** using central finite differences:

$$\nabla \cdot \mathbf{V}[i] = \frac{(V_x^{i+1} - V_x^{i-1}) + (V_y^{j+1} - V_y^{j-1}) + (V_z^{k+1} - V_z^{k-1})}{2h}$$

Face-adjacent wall voxels contribute zero velocity (no-penetration boundary condition).

**Step 2 — Solve the pressure Poisson equation** $\nabla^2 p = \nabla \cdot \mathbf{V} / \Delta t$ via Jacobi iteration:

$$p^{(n+1)}[i] = \frac{p_L + p_R + p_D + p_U + p_B + p_F - (\nabla \cdot \mathbf{V})[i] \cdot h^2}{6}$$

At solid boundaries the missing neighbour pressure is replaced by the current voxel's pressure (Neumann zero-gradient condition). The solver runs **60 Jacobi iterations per frame**, chosen empirically as the point at which the pressure solution is visually converged — beyond 60 there is no noticeable improvement in smoke behaviour, and fewer than ~30 produces visible compressibility artifacts (smoke passing through thin walls or diverging at corners).

**Step 3 — Project velocity** by subtracting the pressure gradient:

$$\mathbf{V}_{new}[i] = \mathbf{V}[i] - \frac{1}{2h}\begin{pmatrix}p_{i+1} - p_{i-1} \\ p_{j+1} - p_{j-1} \\ p_{k+1} - p_{k-1}\end{pmatrix}$$

After projection, velocity components pointing into a wall face are zeroed (wall deflection). A per-frame decay of $\lambda = 0.9999$ models viscous dissipation.

### 4.3 Temperature and Buoyancy

Temperature is packed into the `.w` component of the velocity SSBO and advected with the same semi-Lagrangian backtrace as velocity (Section 4.5). After advection it is cooled toward ambient (zero) via Newton's Law of Cooling:

$$T_{new} = T_{old} \cdot e^{-r_{cool} \cdot \Delta t}, \quad r_{cool} = 0.01$$

This exponential decay models the gradual heat loss of real smoke as it mixes with cooler ambient air. The cooling rate of 0.01 was chosen so that temperature decays to near zero over roughly 10–15 seconds, matching the time scale over which CS2 smoke transitions from an active expanding phase to a slowly settling phase.

The system provides **two buoyancy modes** selectable via the debug GUI:

**Mode 0 — Legacy parabolic buoyancy** (density-based, useful when temperature is disabled):

$$\ell(\rho) = \max\bigl((\rho - \rho_0)(\rho_1 - \rho),\; 0\bigr), \quad \rho_0 = 0.5,\; \rho_1 = 0.9$$

$$a_y^{(0)} = F_{buoy} \cdot \ell(\rho) - F_{grav}$$

The parabolic window $[\rho_0, \rho_1]$ concentrates lift on mid-density voxels: sparse wisps (below 0.5) and the dense core (above 0.9) receive minimal lift. Only the mid-density body of the cloud rises, producing a realistic column profile. This mode is retained as a fallback because it produces stable-looking behaviour even without temperature injection.

**Mode 1 — Heat-based buoyancy** (physically motivated):

$$a_y^{(1)} = F_{temp} \cdot \max(T, 0) - F_{grav}$$

Hot voxels ($T > 0$) experience upward lift proportional to their temperature. As the temperature field dissipates (via the cooling rate above), the buoyant force naturally weakens over time — the smoke rises strongly at first and then settles, without requiring any explicit time-based override. This is the preferred mode as it ties buoyancy to the physical heat of the smoke rather than an ad-hoc density window.

| Parameter | Default | Effect of increasing | Effect of decreasing |
|---|---|---|---|
| `gravityStrength` | `0.05` | Smoke sinks faster; gravity dominates over buoyancy | Smoke floats freely; appears weightless |
| `buoyancyStrength` (mode 0) | `1.0` | Stronger mid-density rise | Weaker column formation; smoke spreads laterally more |
| `densityLow` / `densityHigh` (mode 0) | `0.5` / `0.9` | Shifts which density band receives lift | Changing the band width affects how much of the cloud rises vs stays flat |
| `tempBuoyancyStrength` (mode 1) | `1.0` | Hotter smoke rises more vigorously | Gentle buoyant drift |
| `smokeCoolingRate` | `0.01` | Temperature dissipates faster; buoyant rise fades sooner | Temperature persists longer; smoke keeps rising |

The **gravity of 0.05** is intentionally weak relative to buoyancy of 1.0. This is because the simulation is not using real physical units — the values are tuned visually. A gravity equal to buoyancy would cause the smoke to stay nearly flat, which does not match CS2 reference footage where the smoke clearly billows upward.

### 4.4 Baroclinic Torque (Vorticity)

In a real fluid, vorticity is generated at the interface between regions of different density and temperature — this is the **baroclinic torque** mechanism. Physically, if a pocket of hot smoke sits next to cool air, the pressure gradient across the interface is misaligned with the density gradient, generating a torque that causes the interface to roll up into vortices. This produces the characteristic swirling filaments at smoke boundaries.

The baroclinic term is computed as the cross product of the density gradient and the temperature gradient:

$$\boldsymbol{\tau}_{baro} = \nabla \rho \times \nabla T$$

Both gradients are evaluated using central finite differences across the 6-connected neighbourhood:

$$\nabla \rho = \frac{1}{2h}\begin{pmatrix}\rho_{i+1} - \rho_{i-1} \\ \rho_{j+1} - \rho_{j-1} \\ \rho_{k+1} - \rho_{k-1}\end{pmatrix}, \qquad \nabla T = \frac{1}{2h}\begin{pmatrix}T_{i+1} - T_{i-1} \\ T_{j+1} - T_{j-1} \\ T_{k+1} - T_{k-1}\end{pmatrix}$$

The resulting torque vector is added to the velocity:

$$\mathbf{V} \leftarrow \mathbf{V} + \kappa \cdot \boldsymbol{\tau}_{baro} \cdot \Delta t, \quad \kappa = 0.15$$

The strength $\kappa = 0.15$ was chosen to produce visible but not overwhelming vortex filaments at the smoke boundary. Too high a value causes the smoke to break into chaotic spinning fragments; too low and the effect is invisible. The baroclinic term is only active when temperature is non-zero, so it naturally fades out as the smoke cools — vorticity is strongest during the initial hot expansion and weakens as the smoke equilibrates.

| Parameter | Default | Effect of increasing | Effect of decreasing |
|---|---|---|---|
| `BaroclinicStrength` | `0.15` | Stronger vortex filaments at hot/cold interfaces; more swirling breakup | Minimal interface rolling; smoke boundaries remain smooth |

### 4.5 Semi-Lagrangian Advection

Both velocity and temperature are advected using semi-Lagrangian back-tracing (Stam, 1999):

$$\mathbf{S}_{new}(\mathbf{x}) = \mathbf{S}_{old}\!\left(\mathbf{x} - \mathbf{V}(\mathbf{x})\,\Delta t\right)$$

where $\mathbf{S} = (\mathbf{V}, T)$ is the full state vector. The back-traced position is sampled via 8-corner trilinear interpolation. Semi-Lagrangian advection is unconditionally stable regardless of timestep size, which is important for a real-time system where $\Delta t$ fluctuates with frame rate.

Smoke density is advected by the same scheme from a separate `AdvectSmoke` shader, with a dissipation multiplier of 0.9995 per frame and a Laplacian diffusion pass:

$$\rho_{new} = \rho + r_{diff} \cdot \Delta t \cdot \frac{\sum_{u \in \mathcal{N}} \rho_u - 6\rho}{h^2}, \quad r_{diff} = 0.001$$

| Parameter | Default | Effect of increasing | Effect of decreasing |
|---|---|---|---|
| `smokeFallOff` | `0.9995` | Smoke dissipates faster per frame; cloud clears more quickly | Smoke persists longer; density accumulates |
| `smokeDiffusionRate` | `0.001` | Density spreads more aggressively into neighbouring voxels; sharper boundaries blur | Less diffusion; density boundaries stay crisper |

**`smokeFallOff = 0.9995`** means smoke loses 0.05% of its density per frame. At 60 fps this corresponds to a half-life of roughly 23 seconds, which matches the CS2 smoke grenade duration of approximately 18 seconds before the smoke fully clears.

### 4.6 Velocity Seeding from Flood Fill

The flood fill wavefront injects radial outward velocity into every voxel it reaches, but only during the first 2.5 seconds (the expansion phase). This creates the initial blast wave. The injected velocity at voxel $\mathbf{x}$ is:

$$\mathbf{V}_{inject} = \hat{\mathbf{d}} \cdot S \cdot w \cdot b + \boldsymbol{\varepsilon}$$

where:
- $\hat{\mathbf{d}} = \texttt{normalize}(\mathbf{x} - \mathbf{x}_{seed})$ is the radial direction
- $w = \rho_{flood} \cdot \max(0,\; 1 - r/r_{max})$ combines density with radial falloff
- $b = \text{mix}(1.0,\; 0.65,\; \texttt{smoothstep}(0.85,\; 1.0,\; \max(|d_x|, |d_y|, |d_z|)))$ reduces strength along grid-aligned directions to counteract 6-connected symmetry artifacts
- $\boldsymbol{\varepsilon} = \hat{\mathbf{r}} \cdot 0.15 \cdot w$ is per-voxel random jitter using a `fract(sin(dot(...)))` hash, breaking up the lattice structure

The **velocity inject strength of 0.1** (reduced from the previous 1.0) was necessary because the higher value produced an unrealistically violent outward blast that dominated the fluid solver — the smoke shot to the room walls in under a second. At 0.1 the initial expansion looks more like the CS2 reference where the smoke expands steadily over 1–2 seconds.

---

## 5. Summary

| Subsystem | Method | Key Reference | Why Not Alternative |
|---|---|---|---|
| Voxelization | 13-axis SAT, GPU per-triangle | Schwarz & Seidel (2010) | Conservative rasterization requires extension; z-parity fill requires closed mesh |
| Temporal expansion | Cubic ease-out $1-(1-t)^3$ | — | Linear ramp fills corners at wrong rate; quadratic ease too symmetric |
| Spatial constraint | Oblate ellipsoid gate in normalised coords | Gunnell (2023) | Pure anisotropic decay produces octahedral L1 iso-surfaces |
| Density iso-surfaces | Euclidean distance in ellipsoid space | — | Hop-count (L1) produces pyramid cross-sections |
| Flood fill blocking | 6-connected BFS with wall mask | — | 26-connected allows diagonal tunnelling through 1-voxel walls |
| Noise | Perlin-Worley FBM blend + domain warp + erosion | Worley (1996) | Pure Worley lacks large-scale variation; pure Perlin too smooth for convective puffs |
| Transmittance | Beer-Lambert exponential | Kajiya & Von Herzen (1984), Max (1995) | Linear absorption incorrect for thick media |
| Phase function | HG + Rayleigh continuous blend, $g = 0.5$ | Henyey & Greenstein (1941) | Isotropic underestimates forward-scatter lobe |
| Self-shadowing | 16-step shadow ray + powder effect | Schneider & Vines (2015) | Full multiple-scattering too expensive; Lambert-only too flat |
| Fluid dynamics | Pressure-projection Navier-Stokes, 60 Jacobi iters | Stam (1999); Bridson (2008) | Simple advection without pressure solve produces compressible, wall-penetrating flow |
| Buoyancy | Heat-based ($T$-proportional) or legacy parabolic density window | — | Uniform buoyancy lifts entire volume equally; heat-based naturally fades with cooling |
| Baroclinic torque | $\nabla\rho \times \nabla T$ cross product | — | No baroclinic term produces smooth, laminar boundaries with no vortex roll-up |
| Velocity seeding | Radial inject (0–2.5 s) + axis-bias correction + jitter | — | Pure radial inject creates visible axis-aligned symmetry artifacts |
| Density advection | Semi-Lagrangian back-trace + trilinear interpolation | Stam (1999) | Forward scatter causes gaps; explicit Euler unstable for large $\Delta t$ |

---

## References

Bridson, R. (2008). *Fluid simulation for computer graphics*. CRC Press.

Chandrasekhar, S. (1960). *Radiative transfer*. Dover Publications.

Gottschalk, S., Lin, M. C., & Manocha, D. (1996). OBBTree: A hierarchical structure for rapid interference detection. *Proceedings of the 23rd Annual Conference on Computer Graphics and Interactive Techniques (SIGGRAPH '96)*, 171–180. https://doi.org/10.1145/237170.237244

Gunnell, G. (2023). *CS2 smoke grenades* [Open-source Unity recreation and breakdown video]. GitHub. https://github.com/GarrettGunnell/CS2-Smoke-Grenades

Henyey, L. G., & Greenstein, J. L. (1941). Diffuse radiation in the galaxy. *The Astrophysical Journal, 93*, 70–83. https://doi.org/10.1086/144246

Kajiya, J. T., & Von Herzen, B. P. (1984). Ray tracing volume densities. *ACM SIGGRAPH Computer Graphics, 18*(3), 165–174. https://doi.org/10.1145/964965.808594

Max, N. (1995). Optical models for direct volume rendering. *IEEE Transactions on Visualization and Computer Graphics, 1*(2), 99–108. https://doi.org/10.1109/2945.468400

Pharr, M., Jakob, W., & Humphreys, G. (2023). *Physically based rendering: From theory to implementation* (4th ed.). MIT Press. https://www.pbr-book.org

Schneider, J., & Vines, N. (2015). Real-time volumetric cloudscapes. In W. Engel (Ed.), *GPU Pro 7: Advanced Rendering Techniques* (pp. 97–127). CRC Press.

Schwarz, M., & Seidel, H.-P. (2010). Fast parallel surface and solid voxelization on GPUs. *ACM Transactions on Graphics (SIGGRAPH Asia), 29*(6), Article 179. https://doi.org/10.1145/1882261.1866201

Stam, J. (1999). Stable fluids. *Proceedings of the 26th Annual Conference on Computer Graphics and Interactive Techniques (SIGGRAPH '99)*, 121–128. https://doi.org/10.1145/311535.311548

Worley, S. (1996). A cellular texture basis function. *Proceedings of the 23rd Annual Conference on Computer Graphics and Interactive Techniques (SIGGRAPH '96)*, 291–294. https://doi.org/10.1145/237170.237267

Wrenninge, M. (2012). *Production volume rendering: Design and implementation*. CRC Press.

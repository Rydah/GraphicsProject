#ifndef WORLEY_NOISE_H
#define WORLEY_NOISE_H

#include "core/ComputeShader.h"
#include "core/Texture3D.h"
#include "glVersion.h"

class WorleyNoise {
public:
    Texture3D texture;
    int resolution = 128;

    void init(int res = 128) {
        resolution = res;
        texture.create(res, res, res, GL_R16F);

        const char* src = GLSL_VERSION_CORE 
        R"(
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
layout(binding = 0, r16f) uniform image3D u_Output;

uniform float u_Time;
uniform int   u_Resolution;
uniform int   u_CellCount;    // number of cells per axis (e.g. 4)
uniform int   u_Octaves;      // fBm octaves (e.g. 3)
uniform float u_Persistence;  // amplitude decay per octave (e.g. 0.5)
uniform float u_Speed;        // animation speed

// Hugo Elias integer hash
float hash(int n) {
    n = (n << 13) ^ n;
    n = n * (n * n * 15731 + 789221) + 1376312589;
    return float(n & 0x7FFFFFFF) / float(0x7FFFFFFF);
}

// 3D hash -> vec3 in [0,1], tiled at 'wrap' cells
vec3 hashCell(ivec3 c, int wrap) {
    c = ((c % wrap) + wrap) % wrap;
    int n = c.x + c.y * 137 + c.z * 7919;
    return vec3(hash(n), hash(n + 1), hash(n + 2));
}

// Perlin gradient hash -> unit vec3 gradient
vec3 gradHash(ivec3 c, int wrap) {
    c = ((c % wrap) + wrap) % wrap;
    int n  = c.x + c.y * 137 + c.z * 7919;
    int n2 = (n  << 13) ^ n;  n2 = n2 * (n2 * n2 * 15731 + 789221) + 1376312589;
    int n3 = (n2 << 13) ^ n2; n3 = n3 * (n3 * n3 * 15731 + 789221) + 1376312589;
    int n4 = (n3 << 13) ^ n3; n4 = n4 * (n4 * n4 * 15731 + 789221) + 1376312589;
    return normalize(vec3(
        float(n2 & 0x7FFFFFFF) / float(0x7FFFFFFF) * 2.0 - 1.0,
        float(n3 & 0x7FFFFFFF) / float(0x7FFFFFFF) * 2.0 - 1.0,
        float(n4 & 0x7FFFFFFF) / float(0x7FFFFFFF) * 2.0 - 1.0
    ));
}

// Quintic fade for Perlin
float fade(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

// Tiled Perlin noise, returns [0,1]
float perlin(vec3 pos, int cellCount) {
    vec3 sc = pos * float(cellCount);
    ivec3 c0 = ivec3(floor(sc));
    vec3 f = sc - vec3(c0);
    vec3 u = vec3(fade(f.x), fade(f.y), fade(f.z));

    float n000 = dot(gradHash(c0,                    cellCount), f - vec3(0,0,0));
    float n100 = dot(gradHash(c0 + ivec3(1,0,0),    cellCount), f - vec3(1,0,0));
    float n010 = dot(gradHash(c0 + ivec3(0,1,0),    cellCount), f - vec3(0,1,0));
    float n110 = dot(gradHash(c0 + ivec3(1,1,0),    cellCount), f - vec3(1,1,0));
    float n001 = dot(gradHash(c0 + ivec3(0,0,1),    cellCount), f - vec3(0,0,1));
    float n101 = dot(gradHash(c0 + ivec3(1,0,1),    cellCount), f - vec3(1,0,1));
    float n011 = dot(gradHash(c0 + ivec3(0,1,1),    cellCount), f - vec3(0,1,1));
    float n111 = dot(gradHash(c0 + ivec3(1,1,1),    cellCount), f - vec3(1,1,1));

    float v = mix(
        mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
        mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y),
        u.z
    );
    return clamp(v * 0.5 + 0.5, 0.0, 1.0);
}

// Single-octave tiled Worley noise, bright at cell centers
float worley(vec3 pos, int cellCount) {
    vec3 scaled = pos * float(cellCount);
    ivec3 cell = ivec3(floor(scaled));
    vec3 fr = scaled - vec3(cell);

    float minDist = 1e10;
    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
        ivec3 neighbor = cell + ivec3(dx, dy, dz);
        vec3 fp = vec3(ivec3(dx, dy, dz)) + hashCell(neighbor, cellCount) - fr;
        minDist = min(minDist, length(fp));
    }

    float v = clamp(1.0 - minDist, 0.0, 1.0);
    return v * v * v;
}

void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(coord, ivec3(u_Resolution)))) return;

    vec3 pos = (vec3(coord) + 0.5) / float(u_Resolution);
    pos += vec3(u_Time * u_Speed, u_Time * u_Speed * 0.7, u_Time * u_Speed * 0.3);

    float noise = 0.0;
    float amplitude = 1.0;
    float totalAmplitude = 0.0;
    int cells = u_CellCount;

    for (int i = 0; i < u_Octaves; i++) {
        vec3 warpedPos = pos + float(i) * vec3(0.37, 0.51, 0.29);

        float w = worley(warpedPos, cells);
        // Perlin at half cell count: coarser connected structure
        float p = perlin(warpedPos, max(cells / 2, 1));

        // Perlin-Worley: soft multiply blend.
        // Perlin (0.4..1.4 range) modulates Worley: high Perlin = bright puff cluster,
        // low Perlin = dimmed. Using (0.4 + p) instead of p alone avoids fully
        // zeroing out Worley in low-Perlin regions, preventing too many empty holes.
        float pw = clamp(w * (0.4 + p), 0.0, 1.0);

        noise += amplitude * pw;
        totalAmplitude += amplitude;
        amplitude *= u_Persistence;
        cells *= 2;
    }

    noise /= totalAmplitude;

    imageStore(u_Output, coord, vec4(noise));
}
)";

        cs.setUp(src);
    }

    void generate(float time) {
        texture.bindImage(0, GL_WRITE_ONLY);
        cs.use();
        cs.setFloat("u_Time", time);
        cs.setInt("u_Resolution", resolution);
        cs.setInt("u_CellCount", 4);
        cs.setInt("u_Octaves", 3);
        cs.setFloat("u_Persistence", 0.5f);
        cs.setFloat("u_Speed", 0.025f);
        cs.dispatch(resolution, resolution, resolution);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    void destroy() {
        texture.destroy();
        glDeleteProgram(cs.ID);
    }

private:
    ComputeShader cs;
};

#endif // WORLEY_NOISE_H

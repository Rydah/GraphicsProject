#ifndef WORLEY_NOISE_H
#define WORLEY_NOISE_H

#include "ComputeShader.h"
#include "Texture3D.h"

class WorleyNoise {
public:
    Texture3D texture;
    int resolution = 128;

    void init(int res = 128) {
        resolution = res;
        texture.create(res, res, res, GL_R16F);

        const char* src = R"(
#version 430 core
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
layout(binding = 0, r16f) uniform image3D u_Output;

uniform float u_Time;
uniform int   u_Resolution;
uniform int   u_CellCount;    // number of cells per axis (e.g. 4)
uniform int   u_Octaves;      // fBm octaves (e.g. 3)
uniform float u_Persistence;  // amplitude decay per octave (e.g. 0.5)
uniform float u_Speed;        // animation speed

// Hugo Elias hash
float hash(int n) {
    n = (n << 13) ^ n;
    n = n * (n * n * 15731 + 789221) + 1376312589;
    return float(n & 0x7FFFFFFF) / float(0x7FFFFFFF);
}

// 3D hash from integer cell coords - returns vec3 in [0,1]
vec3 hashCell(ivec3 cell, int wrap) {
    // Wrap for tiling
    cell = ((cell % wrap) + wrap) % wrap;
    int n = cell.x + cell.y * 137 + cell.z * 7919;
    return vec3(hash(n), hash(n + 1), hash(n + 2));
}

// Single-octave tiled Worley noise
float worley(vec3 pos, int cellCount) {
    vec3 scaled = pos * float(cellCount);
    ivec3 cell = ivec3(floor(scaled));
    vec3 frac = scaled - vec3(cell);

    float minDist = 1e10;

    // Check 3x3x3 neighborhood
    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
        ivec3 neighbor = cell + ivec3(dx, dy, dz);
        vec3 featurePoint = vec3(ivec3(dx, dy, dz)) + hashCell(neighbor, cellCount) - frac;
        float dist = length(featurePoint);
        minDist = min(minDist, dist);
    }

    // Invert and sharpen: (1 - dist)^6
    float v = clamp(1.0 - minDist, 0.0, 1.0);
    return v * v * v * v * v * v;
}

void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(coord, ivec3(u_Resolution)))) return;

    // Normalized position [0,1]
    vec3 pos = (vec3(coord) + 0.5) / float(u_Resolution);

    // Animate by offsetting position
    pos += vec3(u_Time * u_Speed, u_Time * u_Speed * 0.7, u_Time * u_Speed * 0.3);

    // fBm: accumulate multiple octaves
    float noise = 0.0;
    float amplitude = 1.0;
    float totalAmplitude = 0.0;
    int cells = u_CellCount;

    for (int i = 0; i < u_Octaves; i++) {
        // Domain warp: offset per octave for swirling effect
        vec3 warpedPos = pos + float(i) * vec3(0.37, 0.51, 0.29);
        noise += amplitude * worley(warpedPos, cells);
        totalAmplitude += amplitude;
        amplitude *= u_Persistence;
        cells *= 2;  // lacunarity = 2
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
        cs.setFloat("u_Speed", 0.05f);
        cs.dispatch(resolution, resolution, resolution);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    void destroy() {
        texture.destroy();
        glDeleteProgram(cs.ID);
    }

private:
    ComputeShader cs;
};

#endif // WORLEY_NOISE_H

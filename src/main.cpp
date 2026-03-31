////////////////////////////////////////////////////////////////////////
//
//  SUTD 50.017 - CS2 Volumetric Smoke (OpenGL 4.3)
//
////////////////////////////////////////////////////////////////////////

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>

// --- ImGui ---
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

// --- Project headers ---
#include "Debugtest/GLDebug.h"            // enableGLDebug(), printGPUInfo()
#include "camera/OrbitCamera.h"           // OrbitCamera struct
#include "Debugtest/SelfTests.h"          // SelfTests::runAllTests()
#include "Debugtest/NoiseDebugView.h"     // NoiseDebugView (Worley slice visualizer)
#include "Debugtest/VelocityDebugView.h"
#include "Debugtest/DepthDebugView.h"      // DepthDebugView (linearized depth visualizer)

#include "core/ComputeShader.h"
#include "core/Buffer.h"
#include "core/Texture3D.h"
#include "core/Texture2D.h"
#include "core/Framebuffer.h"
#include "core/FullscreenQuad.h"
#include "core/smokeField.h"

#include "Procedural/WorleyNoise.h"
#include "Procedural/FloodFill.h"
#include "Procedural/ProceduralSmokeSystem.h"

#include "Voxel/Voxelizer.h"
#include "Voxel/VoxelDebug.h"

#include "SmokeSolver/SmokeSolver.h"
#include "core/SceneDepthPass.h"
#include "Rendering/Raymarcher.h"
#include "Rendering/LightSource.h"

//---------------------------------------------------------------------
// Window
//---------------------------------------------------------------------
static unsigned int winWidth  = 800;
static unsigned int winHeight = 600;

//---------------------------------------------------------------------
// Scene-level globals (need to be accessible from GLFW callbacks)
//---------------------------------------------------------------------
static OrbitCamera      g_camera;
static Voxelizer*       g_voxelizer   = nullptr;
static VoxelFloodFill*  g_floodFill   = nullptr;
static NoiseDebugView    g_noiseView;
static VelocityDebugView g_velocityDebug;
static DepthDebugView    g_depthDebug;
static SceneDepthPass*   g_depthPass = nullptr;
static bool              g_raymarchEnabled = true;
static std::vector<int>  g_wallVoxelCache;
static LightSource       g_light;

// Ray-AABB slab intersection. Returns true on hit with [tEnter, tExit].
static bool rayIntersectsAABB(const glm::vec3& rayOrigin,
                              const glm::vec3& rayDir,
                              const glm::vec3& bmin,
                              const glm::vec3& bmax,
                              float& tEnter,
                              float& tExit)
{
    glm::vec3 invDir = 1.0f / rayDir;
    glm::vec3 t1 = (bmin - rayOrigin) * invDir;
    glm::vec3 t2 = (bmax - rayOrigin) * invDir;

    glm::vec3 tMin = glm::min(t1, t2);
    glm::vec3 tMax = glm::max(t1, t2);

    tEnter = glm::max(glm::max(tMin.x, tMin.y), tMin.z);
    tExit  = glm::min(glm::min(tMax.x, tMax.y), tMax.z);

    return (tExit >= tEnter) && (tExit >= 0.0f);
}

//---------------------------------------------------------------------
// GLFW callbacks
//---------------------------------------------------------------------
static void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
    winWidth  = (unsigned int)w;
    winHeight = (unsigned int)h;
}

static void key_callback(GLFWwindow* window, int key, int /*scan*/, int action, int /*mods*/) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // Noise debug toggle
    if (key == GLFW_KEY_N && action == GLFW_PRESS) {
        g_noiseView.enabled = !g_noiseView.enabled;

        // Keep debug views mutually exclusive
        if (g_noiseView.enabled) {
            g_velocityDebug.enabled = false;
            g_depthDebug.enabled    = false;
        }

        std::cout << "Noise debug: " << (g_noiseView.enabled ? "ON" : "OFF") << "\n";
    }

    // Velocity debug toggle
    if (key == GLFW_KEY_V && action == GLFW_PRESS) {
        g_velocityDebug.enabled = !g_velocityDebug.enabled;

        // Keep debug views mutually exclusive
        if (g_velocityDebug.enabled) {
            g_noiseView.enabled  = false;
            g_depthDebug.enabled = false;
        }

        std::cout << "Velocity debug: " << (g_velocityDebug.enabled ? "ON" : "OFF") << "\n";
    }

    // Noise slice controls only when noise debug is active
    if (g_noiseView.enabled) {
        if (key == GLFW_KEY_UP && (action == GLFW_PRESS || action == GLFW_REPEAT))
            g_noiseView.sliceZ = glm::min(g_noiseView.sliceZ + 0.02f, 1.0f);

        if (key == GLFW_KEY_DOWN && (action == GLFW_PRESS || action == GLFW_REPEAT))
            g_noiseView.sliceZ = glm::max(g_noiseView.sliceZ - 0.02f, 0.0f);
    }

    // Ray march toggle
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        g_raymarchEnabled = !g_raymarchEnabled;

        // Keep debug views mutually exclusive when ray march is on
        if (g_raymarchEnabled) {
            g_noiseView.enabled     = false;
            g_velocityDebug.enabled = false;
            g_depthDebug.enabled    = false;
        }

        std::cout << "Raymarcher: " << (g_raymarchEnabled ? "ON" : "OFF") << "\n";
    }

    // Depth debug toggle
    if (key == GLFW_KEY_F && action == GLFW_PRESS) {
        g_depthDebug.enabled = !g_depthDebug.enabled;

        // Keep debug views mutually exclusive
        if (g_depthDebug.enabled) {
            g_noiseView.enabled     = false;
            g_velocityDebug.enabled = false;
        }

        std::cout << "Depth (framebuffer) debug: " << (g_depthDebug.enabled ? "ON" : "OFF") << "\n";
    }

}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int) {
    g_camera.onMouseButton(button, action);

    // Right click: seed smoke at clicked point in the voxel domain.
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        if (!g_voxelizer || !g_floodFill) return;

        double mouseX = 0.0, mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        float aspect = (float)winWidth / (float)winHeight;
        glm::mat4 view = g_camera.view();
        glm::mat4 proj = g_camera.proj(aspect);
        glm::mat4 invView = glm::inverse(view);
        glm::mat4 invProj = glm::inverse(proj);

        float ndcX = (2.0f * (float)mouseX / (float)winWidth) - 1.0f;
        float ndcY = 1.0f - (2.0f * (float)mouseY / (float)winHeight);

        glm::vec4 vDir = invProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        vDir.w = 0.0f;

        glm::vec3 rayDir    = glm::normalize(glm::vec3(invView * vDir));
        glm::vec3 rayOrigin = glm::vec3(invView[3]);

        const VoxelDomain& domain = g_voxelizer->domain;

        float tEnter = 0.0f, tExit = 0.0f;
        if (!rayIntersectsAABB(rayOrigin, rayDir, domain.boundsMin, domain.boundsMax, tEnter, tExit)) {
            return;
        }

        float t = glm::max(tEnter, 0.0f);
        float step = glm::max(domain.voxelSize * 0.5f, 1e-4f);
        int maxSteps = glm::clamp((int)glm::ceil((tExit - t) / step) + 2, 1, 4096);

        // March through air, track the last air voxel before a surface.
        // Skip any solid voxels at the ray entry (outer shell), then seed
        // at the last air voxel before the ray hits an interior wall or floor.
        glm::ivec3 lastAirVoxel(-1);
        bool inAir = false;
        bool seeded = false;

        for (int i = 0; i < maxSteps; ++i) {
            glm::vec3 worldPos = rayOrigin + rayDir * t;
            glm::ivec3 c = domain.worldToGrid(worldPos);
            int idx = domain.flatten(c);

            if (idx >= 0 && idx < (int)g_wallVoxelCache.size()) {
                int voxVal = g_wallVoxelCache[idx];
                if (voxVal == 0) {
                    // Air - keep advancing and remember this voxel
                    inAir = true;
                    lastAirVoxel = c;
                } else if (inAir) {
                    // Hit a solid surface after passing through air - seed here
                    glm::vec3 seedPos = domain.gridToWorldCenter(glm::vec3(lastAirVoxel));
                    g_floodFill->seed(seedPos, domain.gridSize, domain.boundsMin, domain.voxelSize);
                    seeded = true;
                    break;
                }
                // else: still traversing outer shell before entering interior - skip
            }

            t += step;
            if (t > tExit) break;
        }

        if (!seeded) {
            std::cout << "Click hit no valid surface for smoke seed.\n";
        }
    }
}

static void cursor_pos_callback(GLFWwindow* window, double x, double y) {
    float fx = (float)x, fy = (float)y;

    // Hold L + left-drag: unproject mouse onto a horizontal plane at the
    // light's current Y, so the dot follows the cursor exactly.
    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS &&
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
    {
        float aspect  = (float)winWidth / (float)winHeight;
        glm::mat4 view = g_camera.view();
        glm::mat4 proj = g_camera.proj(aspect);
        glm::mat4 invVP = glm::inverse(proj * view);

        // NDC mouse position
        float ndcX =  (2.0f * fx / (float)winWidth)  - 1.0f;
        float ndcY = -(2.0f * fy / (float)winHeight) + 1.0f;

        // Unproject near and far points to get a world-space ray
        glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        glm::vec4 farH  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
        glm::vec3 rayOrigin = glm::vec3(nearH) / nearH.w;
        glm::vec3 rayDir    = glm::normalize(glm::vec3(farH) / farH.w - rayOrigin);

        // Intersect with horizontal plane y = light.position.y
        float planeY = g_light.position.y;
        if (std::abs(rayDir.y) > 1e-4f) {
            float t = (planeY - rayOrigin.y) / rayDir.y;
            if (t > 0.0f) {
                glm::vec3 hit = rayOrigin + t * rayDir;
                g_light.position.x = hit.x;
                g_light.position.z = hit.z;
                g_light.orbitEnabled = false;
                g_light.syncAngles();   // keep ImGui sliders in sync
                std::cout << "[Light] pos=("
                          << g_light.position.x << ", "
                          << g_light.position.y << ", "
                          << g_light.position.z << ")\n";
            }
        }
        return;
    }

    g_camera.onMouseMove(window, fx, fy);
}

static void scroll_callback(GLFWwindow* window, double, double dy) {
    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) {
        g_light.position.y += (float)dy * 0.2f;
        g_light.orbitEnabled = false;
        g_light.syncAngles();
        std::cout << "[Light] pos=("
                  << g_light.position.x << ", "
                  << g_light.position.y << ", "
                  << g_light.position.z << ")\n";
        return;
    }
    g_camera.onScroll((float)dy);
}

//---------------------------------------------------------------------
// main
//---------------------------------------------------------------------
int main()
{
    // --- Window + context ---
    glfwInit();
#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#endif
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

    // glfw window creation
    // --------------------
    GLFWwindow* window = glfwCreateWindow(winWidth, winHeight, "CS2 Volumetric Smoke", NULL, NULL);
    if (!window) {
        std::cout << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetScrollCallback(window, scroll_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD\n";
        return -1;
    }

    // --- Debug + GPU info ---
    enableGLDebug();
    printGPUInfo();

    // --- Startup self-tests ---
    SelfTests::runAllTests();

    // --- ImGui init ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    glEnable(GL_DEPTH_TEST);

    g_light.initMarker();

    // --- Worley noise ---
    WorleyNoise worleyNoise;
    worleyNoise.init(128);

    FullscreenQuad fsQuad;
    fsQuad.init();

    g_noiseView.init();
    g_velocityDebug.init();
    g_depthDebug.init();

    // --- Voxel scene (procedural test arena) ---
    Voxelizer voxelizer;
    voxelizer.generateTestScene(0.15f, 96, 32, 96);
    g_wallVoxelCache = voxelizer.staticVoxels.download<int>(voxelizer.domain.totalVoxels);

    VoxelDebug voxelDebug;
    voxelDebug.init();

    // --- Flood fill ---
    VoxelFloodFill floodFill;
    floodFill.init(voxelizer.domain.totalVoxels);

    // --- Scene depth pass ---
    SceneDepthPass depthPass;
    depthPass.init(winWidth, winHeight);
    g_depthPass = &depthPass;

    // --- Raymarcher ---
    Raymarcher raymarcher;
    raymarcher.init(winWidth, winHeight);

    g_voxelizer = &voxelizer;
    g_floodFill = &floodFill;

    // --- Smoke solver state ---
    SmokeField smoke;
    smoke.init(voxelizer.domain);


    // // --- START DEBUG --- Just to seed the initial velocities for the smoke solver for debugging remove after integration with the floodfill
    // std::vector<glm::vec4> initVel(voxelizer.domain.totalVoxels, glm::vec4(0.0f));

    // for (int z = 0; z < voxelizer.domain.gridSize.z; ++z) {
    //     for (int y = 0; y < voxelizer.domain.gridSize.y; ++y) {
    //         for (int x = 0; x < voxelizer.domain.gridSize.x; ++x) {
    //             glm::ivec3 c(x, y, z);
    //             int idx = voxelizer.domain.flatten(c);

    //             // small test region near one corner
    //             if (x >= 4 && x <= 8 &&
    //                 y >= 1 && y <= 4 &&
    //                 z >= 4 && z <= 8) {
    //                 initVel[idx] = glm::vec4(1.0f, -2.0f, 1.0f, 0.0f);
    //             }
    //         }
    //     }
    // }
    
    // smoke.velocity1.upload(initVel);
    // smoke.velocity2.upload(initVel);
    // // --- END DEBUG ---

    SmokeSolver solver;
    solver.init();

    // --- PSmokeSys ---
    ProceduralSmokeSystem smokeSystem;
    smokeSystem.init();

    // --- Timing ---
    float lastFrameTime = (float)glfwGetTime();

    // --- Render loop ---
    while (!glfwWindowShouldClose(window))
    {
        float time = (float)glfwGetTime();
        float dt   = time - lastFrameTime;
        lastFrameTime = time;

        glfwPollEvents();

        // --- WASD camera movement ---
        {
            glm::vec3 camPos  = g_camera.position();
            glm::vec3 forward = glm::normalize(g_camera.target - camPos);
            glm::vec3 right   = glm::normalize(glm::cross(forward, g_camera.up));
            // Flatten to XZ so WASD feels like walking, not flying
            forward.y = 0.0f; if (glm::length(forward) > 1e-4f) forward = glm::normalize(forward);
            right.y   = 0.0f; if (glm::length(right)   > 1e-4f) right   = glm::normalize(right);

            float speed = g_camera.dist * 2.0f * dt;   // scales with zoom level
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) g_camera.target += forward * speed;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) g_camera.target -= forward * speed;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) g_camera.target -= right   * speed;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) g_camera.target += right   * speed;
            if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) g_camera.target.y -= speed;
            if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) g_camera.target.y += speed;
        }

        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Resize passes if window changed
        depthPass.resize(winWidth, winHeight);
        raymarcher.resize(winWidth, winHeight);

        // --- Camera matrices ---
        float aspect = (float)winWidth / (float)winHeight;
        glm::mat4 view = g_camera.view();
        glm::mat4 proj = g_camera.proj(aspect);

        // Render scene depth into FBO
        depthPass.execute(voxelizer.staticVoxels, voxelizer.domain, view, proj);

        // Restore default viewport after depth pass
        glViewport(0, 0, winWidth, winHeight);

        // --- Light update ---
        g_light.update(dt);

        // --- GPU simulation ---
        worleyNoise.generate(time);

        // floodFill.propagate(12,
        //                     voxelizer.domain.gridSize,
        //                     voxelizer.domain.boundsMin,
        //                     voxelizer.domain.voxelSize,
        //                     voxelizer.staticVoxels,
        //                     dt);

        // solver.step(smoke, voxelizer.staticVoxels, dt);
        smokeSystem.update(
            floodFill,
            solver,
            smoke,
            voxelizer.staticVoxels,
            voxelizer.domain,
            dt
        );


        // --- Ray march (default ON; toggle with R) ---
        if (g_raymarchEnabled) {
            // raymarcher.render(
            //     floodFill.currentBuffer(),
            //     voxelizer.staticVoxels,
            //     depthPass.depthTex,
            //     worleyNoise.texture,
            //     voxelizer.domain,
            //     view, proj,
            //     0.001f, 100.0f,
            //     floodFill.effectiveMaxDensity(),
            //     time,
            //     floodFill.seedWorldPos,
            //     floodFill.maxSeedValue,
            //     floodFill.radiusXZ,
            //     floodFill.radiusY
            // );
            raymarcher.render(
                smoke.getSrcDensity(),
                voxelizer.staticVoxels,
                depthPass.depthTex,
                worleyNoise.texture,
                voxelizer.domain,
                view, proj,
                0.001f, 100.0f,
                time,
                g_light
            );
        }

        // --- Debug / render modes ---
        if (g_noiseView.enabled) {
            g_noiseView.draw(worleyNoise.texture, fsQuad);
        }
        else if (g_depthDebug.enabled) {
            g_depthDebug.draw(depthPass.depthTex, fsQuad);
        }
        else if (g_velocityDebug.enabled) {
            g_velocityDebug.draw(
                voxelizer.domain,
                smoke.getSrcVelocity(),
                voxelizer.staticVoxels,
                view, proj
            );
        }
        else {
            // Normal mode:
            // - R ON  -> walls + volumetric raymarched smoke
            // - R OFF -> voxel debug smoke cubes (yellow/orange)
            if (g_raymarchEnabled) {
                voxelDebug.draw(
                    voxelizer.staticVoxels,
                    view, proj,
                    voxelizer.domain.gridSize,
                    voxelizer.domain.boundsMin,
                    voxelizer.domain.voxelSize,
                    g_light
                );
                raymarcher.blit(fsQuad);
            } else {
                voxelDebug.drawWithSmoke(
                    voxelizer.staticVoxels,
                    floodFill.currentBuffer(),
                    view, proj,
                    voxelizer.domain.gridSize,
                    voxelizer.domain.boundsMin,
                    voxelizer.domain.voxelSize,
                    g_light
                );
            }
        }

        // Draw light marker on top of everything
        g_light.drawMarker(view, proj);

        // --- ImGui panel ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("Smoke Grenade");

        // --- Grenade Controls ---
        if (ImGui::CollapsingHeader("Grenade Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Throw Grenade")) {
                glm::vec3 center = (voxelizer.domain.boundsMin + voxelizer.domain.boundsMax) * 0.5f;
                floodFill.seed(center, voxelizer.domain.gridSize,
                               voxelizer.domain.boundsMin, voxelizer.domain.voxelSize);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                floodFill.clear();
                smoke.clear();
            }
            ImGui::TextDisabled("Default seed is at center of the scene. Right-click on surfaces to seed.");
            static int expansionSpeed = 3;
            if (ImGui::SliderInt("Expansion Speed", &expansionSpeed, 1, 8))
                smokeSystem.setFloodFillStepsPerFrame(expansionSpeed);
            ImGui::Checkbox("Advect Smoke", &solver.advectSmokeEnabled);
        }

        // --- Light ---
        if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool changed = false;
            changed |= ImGui::SliderFloat("Azimuth",   &g_light.azimuth,   0.f, 360.f);
            changed |= ImGui::SliderFloat("Elevation", &g_light.elevation, 5.f,  85.f);
            if (changed) g_light.rebuildPosition();

            static float timeOfDay = 0.0f;
            ImGui::Text("Day");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Night").x - ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::SliderFloat("##tod", &timeOfDay, 0.0f, 1.0f)) {
                const glm::vec3 noon   = glm::vec3(1.00f, 0.95f, 0.90f);
                const glm::vec3 sunset = glm::vec3(1.00f, 0.45f, 0.10f);
                const glm::vec3 night  = glm::vec3(0.25f, 0.35f, 0.80f);
                if (timeOfDay < 0.5f)
                    g_light.color = glm::mix(noon,   sunset, timeOfDay * 2.0f);
                else
                    g_light.color = glm::mix(sunset, night,  (timeOfDay - 0.5f) * 2.0f);
            }
            ImGui::SameLine();
            ImGui::Text("Night");

            ImGui::SliderFloat("Intensity", &g_light.intensity,       0.f, 3.f);
            ImGui::SliderFloat("Ambient",   &g_light.ambientStrength, 0.f, 0.8f);

            ImGui::Checkbox("Orbit", &g_light.orbitEnabled);
            if (g_light.orbitEnabled)
                ImGui::SliderFloat("Orbit Speed", &g_light.orbitSpeed, 0.05f, 3.0f);
        }

        // --- Smoke Volume ---
        if (ImGui::CollapsingHeader("Smoke Volume", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Density Scale", &raymarcher.densityScale, 0.1f, 30.0f);
            ImGui::SliderFloat("Scattering Ss", &raymarcher.sigmaS,       0.0f, 10.0f);
            ImGui::SliderFloat("Absorption Sa", &raymarcher.sigmaA,       0.0f, 5.0f);
        }

        // --- Phase Function ---
        if (ImGui::CollapsingHeader("Phase Function", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("HG/Rayleigh Blend", &raymarcher.phaseBlend, 0.0f, 1.0f);
            ImGui::SameLine(); ImGui::TextDisabled("(0=HG  1=Rayleigh)");
            ImGui::SliderFloat("HG Anisotropy g",   &raymarcher.g,         -1.0f, 1.0f);
        }

        // --- Noise & Edge ---
        if (ImGui::CollapsingHeader("Noise & Edge")) {
            ImGui::SliderFloat("Noise Strength", &raymarcher.noiseStrength, 0.0f, 1.0f);
            ImGui::SliderFloat("Noise Scale",    &raymarcher.noiseScale,    0.5f, 8.0f);
            ImGui::SliderFloat("Haze Floor",     &raymarcher.hazeFloor,     0.0f, 1.0f);
            ImGui::SliderFloat("Edge Fade Width",&raymarcher.edgeFadeWidth, 0.05f, 0.6f);
            ImGui::SliderFloat("Curl Strength",  &raymarcher.curlStrength,  0.0f, 4.0f);
        }

        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // --- Cleanup ---
    g_voxelizer  = nullptr;
    g_floodFill  = nullptr;
    g_depthPass  = nullptr;

    g_light.destroyMarker();
    depthPass.destroy();
    raymarcher.destroy();
    solver.destroy();
    smoke.destroy();
    smokeSystem.destroy();

    floodFill.destroy();
    voxelizer.destroy();
    voxelDebug.destroy();
    worleyNoise.destroy();
    fsQuad.destroy();
    g_noiseView.destroy();
    g_velocityDebug.destroy();
    g_depthDebug.destroy();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwTerminate();
    return 0;
}
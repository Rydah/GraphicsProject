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
#include "post/Upsampler.h"
#include "post/Compositor.h"

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

static void rebuildArena(
    Voxelizer& voxelizer,
    VoxelFloodFill& floodFill,
    SmokeField& smoke,
    float voxelSize,
    int gridX, int gridY, int gridZ) {
    // destroy bound objects as they contain references which we are currently using.
    smoke.destroy();
    floodFill.destroy();
    voxelizer.destroy();

    // Rebuild voxel arena
    voxelizer.generateTestScene(voxelSize, gridX, gridY, gridZ);

    // Refresh CPU cache used by mouse picking / seeding
    g_wallVoxelCache = voxelizer.staticVoxels.download<int>(voxelizer.domain.totalVoxels);

    // Reinit floodfill and smoke
    floodFill.init(voxelizer.domain.totalVoxels);
    smoke.init(voxelizer.domain);

    // Optional extra safety if init() does not fully clear contents
    floodFill.clear();
    smoke.clear();

    std::cout << "[Arena] Rebuilt to "
              << voxelizer.domain.gridSize.x << "x"
              << voxelizer.domain.gridSize.y << "x"
              << voxelizer.domain.gridSize.z
              << " @ voxelSize=" << voxelizer.domain.voxelSize
              << std::endl;
}

//---------------------------------------------------------------------
// Helper: (re)build the scene color FBO at the given resolution.
// Called once at startup and whenever the window is resized.
//---------------------------------------------------------------------
static void rebuildSceneFBO(Framebuffer& fbo,
                            Texture2D& colorTex,
                            const Texture2D& depthTex,
                            int w, int h)
{
    colorTex.destroy();
    fbo.destroy();
    colorTex.create(w, h, GL_RGBA8);
    fbo.create();
    fbo.attachColor(colorTex.ID);
    fbo.attachDepth(depthTex.ID);
    if (!fbo.isComplete())
        std::cerr << "[SceneFBO] Framebuffer incomplete after rebuild!\n";
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

    // pending arena settings for ImGUI (we ABSOLUTELY CANNOT allow a slider to constantly destroy and rebuild)
    float pendingVoxelSize = voxelizer.domain.voxelSize;
    int pendingGridX = voxelizer.domain.gridSize.x;
    int pendingGridY = voxelizer.domain.gridSize.y;
    int pendingGridZ = voxelizer.domain.gridSize.z;

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
    int raymarchResolutionMode = 1; // 0=full, 1=half, 2=quarter

    // --- Upsampler (Catmull-Rom bicubic) ---
    Upsampler upsampler;
    upsampler.init(winWidth, winHeight);

    // --- Compositor (sharpening + compositing) ---
    Compositor compositor;
    compositor.init();
    
    // Scene color texture (for compositing)
    // sceneColorTex is now backed by a real Framebuffer so we can
    // render opaque geometry into it before compositing.
    Framebuffer sceneFBO;
    Texture2D   sceneColorTex;
    rebuildSceneFBO(sceneFBO, sceneColorTex, depthPass.depthTex, (int)winWidth, (int)winHeight);

    g_voxelizer = &voxelizer;
    g_floodFill = &floodFill;

    // --- Smoke solver state ---
    SmokeField smoke;
    smoke.init(voxelizer.domain);

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

        // Resize dependent resources when window size changes
        {
            depthPass.resize(winWidth, winHeight);
            raymarcher.resize(winWidth, winHeight);
            upsampler.resize(winWidth, winHeight);
 
            // FIX: rebuild the scene FBO + texture together when the window
            // resizes.  Previously only the texture was recreated (and it was
            // never attached to any FBO), so every resize left a dangling,
            // uninitialised texture being passed to the compositor.
            if ((int)winWidth  != sceneColorTex.width ||
                (int)winHeight != sceneColorTex.height)
            {
                rebuildSceneFBO(sceneFBO, sceneColorTex, depthPass.depthTex, (int)winWidth, (int)winHeight);
            }
        }


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

        // --- Ray march smoke into half-res texture ---
        if (g_raymarchEnabled) {
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

        // -------------------------------------------------------------------
        // FIX: Render opaque scene geometry into sceneFBO so the compositor
        // has a real background to blend smoke over.
        //
        // We need the depth buffer here too so geometry is occluded correctly.
        // Attach the depthPass depth texture as the FBO depth attachment, OR
        // create a dedicated depth renderbuffer for the scene FBO.  The simplest
        // approach that works with the existing code is to render into sceneFBO
        // with its own depth renderbuffer (added to Framebuffer::create() or
        // managed here), then continue using depthPass.depthTex for the
        // raymarcher's depth-clip test (it was already rendered above).
        //
        // Minimal implementation — renders voxelDebug walls into sceneFBO:
        // -------------------------------------------------------------------
        {
            sceneFBO.bind();
            glViewport(0, 0, (int)winWidth, (int)winHeight);
 
            // Clear to the same sky colour used on the default framebuffer
            glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
 
            // Draw all opaque scene geometry.
            // voxelDebug.draw() renders the static wall voxels with Phong
            // shading — this is the background the compositor will composite
            // smoke over.
            voxelDebug.draw(
                voxelizer.staticVoxels,
                view, proj,
                voxelizer.domain.gridSize,
                voxelizer.domain.boundsMin,
                voxelizer.domain.voxelSize,
                g_light
            );
 
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, (int)winWidth, (int)winHeight);
        }
        // -------------------------------------------------------------------
 
        // --- Clear the default framebuffer for the final composite ---
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
 
        // --- Debug / render modes ---
        if (g_noiseView.enabled) {
            g_noiseView.draw(worleyNoise.texture, fsQuad);
        }
        else if (g_depthDebug.enabled) {
            g_depthDebug.draw(depthPass.depthTex, fsQuad);
        }
        else if (g_velocityDebug.enabled) {
            g_velocityDebug.draw(
                voxelizer.domain, smoke.getSrcVelocity(),
                voxelizer.staticVoxels, view, proj
            );
        }
        else {
            // Normal mode:
            // - R ON  -> final composite: sceneColorTex (walls) + volumetric raymarched smoke
            // - R OFF -> voxel debug smoke cubes (yellow/orange)
            if (g_raymarchEnabled) {
                if (raymarchResolutionMode == 0) {
                    // Full-res: no upsampler needed
                    compositor.composite(sceneColorTex, raymarcher.smokeOut, depthPass.depthTex, fsQuad);
                } else {
                    // Half/quarter-res: bilateral depth-aware upsample, then composite
                    upsampler.upsample(raymarcher.smokeOut, depthPass.depthTex, fsQuad, raymarchResolutionMode, 0.001f, 100.0f);
                    compositor.composite(sceneColorTex, upsampler.fullResOutput, depthPass.depthTex, fsQuad);
                }
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

        // --- Frame timing ---
        {
            static float frameSamples[60] = {};
            static int   frameIdx = 0;
            frameSamples[frameIdx] = dt * 1000.0f;
            frameIdx = (frameIdx + 1) % 60;
            float avgMs = 0.0f;
            for (float s : frameSamples) avgMs += s;
            avgMs /= 60.0f;
            ImGui::Text("%.2f ms/frame  (%.0f FPS)", avgMs, 1000.0f / avgMs);
        }
        ImGui::Separator();

        // --- Arena Rebuild Controls ---
        if (ImGui::CollapsingHeader("Arena", ImGuiTableColumnFlags_DefaultHide)) {
            ImGui::SliderFloat("Voxel Size", &pendingVoxelSize, 0.05f, 0.5f);
            ImGui::SliderInt("Grid X", &pendingGridX, 16, 256);
            ImGui::SliderInt("Grid Y", &pendingGridY, 16, 128);
            ImGui::SliderInt("Grid Z", &pendingGridZ, 16, 256);

            ImGui::Text("Current: %d x %d x %d  |  voxelSize %.3f",
                voxelizer.domain.gridSize.x,
                voxelizer.domain.gridSize.y,
                voxelizer.domain.gridSize.z,
                voxelizer.domain.voxelSize);

            bool changed =
                pendingVoxelSize != voxelizer.domain.voxelSize ||
                pendingGridX != voxelizer.domain.gridSize.x ||
                pendingGridY != voxelizer.domain.gridSize.y ||
                pendingGridZ != voxelizer.domain.gridSize.z;

            if (!changed)
                ImGui::BeginDisabled();

            if (ImGui::Button("Rebuild Arena"))
            {
                rebuildArena(
                    voxelizer,
                    floodFill,
                    smoke,
                    pendingVoxelSize,
                    pendingGridX,
                    pendingGridY,
                    pendingGridZ
                );
            }

            if (!changed)
                ImGui::EndDisabled();

            ImGui::TextDisabled("Changes only apply when you click Rebuild Arena.");
        }

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
            static int expansionSpeed = 1;
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

        // --- SMoke Behaviour ---
        if (ImGui::CollapsingHeader("Smoke Behaviour")) {
            float smokeSeedDensity = smokeSystem.getFloodFillSmokeInjectStrength();
            if (ImGui::SliderFloat("Smoke Seed Density", &smokeSeedDensity, 0.0f, 10.0f)) {
                smokeSystem.setFloodFillSmokeInjectStrength(smokeSeedDensity);
            }

            float smokeSeedVelocity = smokeSystem.getFloodFillVelocityInjectStrength();
            if (ImGui::SliderFloat("Smoke Seed Velocity", &smokeSeedVelocity, 0.0f, 10.0f)) {
                smokeSystem.setFloodFillVelocityInjectStrength(smokeSeedVelocity);
            }

            float smokeSeedTemp = smokeSystem.getFloodFillTempInjectStrength();
            if (ImGui::SliderFloat("Smoke Seed Temperature", &smokeSeedTemp, 0.0f, 400.0f)) {
                smokeSystem.setFloodFillTempInjectStrength(smokeSeedTemp);
            }

            float smokeFallOffFloor = 0.9995f;
            float smokeFallOffDelta = 0.0005f;
            float smokeFallOff = (solver.getSmokeFallOff()-smokeFallOffFloor)/smokeFallOffDelta;
            if (ImGui::SliderFloat("Smoke FallOff", &smokeFallOff, 0.0f, 1.0f)) {
                solver.setSmokeFallOff(smokeFallOffFloor + smokeFallOff * smokeFallOffDelta);
            }

            float smokeDiffRate = solver.getSmokeDiffusionRate();
            if (ImGui::SliderFloat("Smoke Diffusion Rate", &smokeDiffRate, 0.0f, 0.1f)) {
                solver.setSmokeDiffsionRate(smokeDiffRate);
            }

            ImGui::SliderInt("Pressure Iterations", &solver.pressureIterations, 0, 2000);

        }

        // --- Forces ---
        if (ImGui::CollapsingHeader("Forces")) {
            bool useHeatBuoyancy = solver.getUseHeatBuoyancy();
            if (ImGui::Checkbox("Use Heat Buoyancy", &useHeatBuoyancy)) {
                solver.setUseHeatBuoyancy(useHeatBuoyancy);
            }

            if (!useHeatBuoyancy)
            {
                float buoyancy = solver.getBuoyancy();
                if (ImGui::SliderFloat("Parabola Buoyancy Strength", &buoyancy, 0.0f, 2.0f)) {
                    solver.setBuoyancy(buoyancy);
                }

                ImGui::Indent();

                float range[2] = {
                    solver.getMinSinkDensity(),
                    solver.getMaxSinkDensity()
                };

                if (ImGui::SliderFloat2("Sink Density Range", range, 0.0f, 1.0f))
                {
                    if (range[0] > range[1])
                        std::swap(range[0], range[1]);

                    solver.setMinSinkDensity(range[0]);
                    solver.setMaxSinkDensity(range[1]);
                }

                ImGui::Unindent();
            }
            else
            {
                float heatBuoyancy = solver.getHeatBuoyancy();
                if (ImGui::SliderFloat("Heat Buoyancy Strength", &heatBuoyancy, 0.0f, 2.0f)) {
                    solver.setHeatBuoyancy(heatBuoyancy);
                }

            }

            float gravity = solver.getGravity();
            if (ImGui::SliderFloat("Gravity Strength", &gravity, 0.0f, 2.0f)) {
                solver.setGravity(gravity);
            }

            float baroClinic = solver.getBaroClinicStrength();
            if (ImGui::SliderFloat("Baroclinic Strength (vorticity)", &baroClinic, 0.0f, 1.0f)) {
                solver.setBaroClinicStrength(baroClinic);
            }
        }

        // --- Phase Function ---
        if (ImGui::CollapsingHeader("Phase Function")) {
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

        // --- Post-Processing (Sharpening & Compositing) ---
        if (ImGui::CollapsingHeader("Post-Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* resItems[] = {
                "1.0x (Full)",
                "0.5x (Half)",
                "0.25x (Quarter)"
            };
            if (ImGui::Combo("Raymarch Resolution", &raymarchResolutionMode, resItems, IM_ARRAYSIZE(resItems))) {
                if (raymarchResolutionMode == 0) raymarcher.resolutionScale = 1.0f;
                if (raymarchResolutionMode == 1) raymarcher.resolutionScale = 0.5f;
                if (raymarchResolutionMode == 2) raymarcher.resolutionScale = 0.25f;
                raymarcher.resize(winWidth, winHeight);
            }

            ImGui::SliderFloat("Sharpen Strength", &compositor.sharpenStrength, 0.0f, 2.0f);
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
    upsampler.destroy();
    compositor.destroy();
    sceneColorTex.destroy();
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
////////////////////////////////////////////////////////////////////////
//
//  SUTD 50.017 - CS2 Volumetric Smoke (OpenGL 4.3)
//
////////////////////////////////////////////////////////////////////////

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

// --- Project headers ---
#include "GLDebug.h"          // enableGLDebug(), printGPUInfo()
#include "OrbitCamera.h"      // OrbitCamera struct
#include "SelfTests.h"        // SelfTests::runAllTests()
#include "NoiseDebugView.h"   // NoiseDebugView (Worley slice visualizer)

#include "ComputeShader.h"
#include "Buffer.h"
#include "Texture3D.h"
#include "Texture2D.h"
#include "Framebuffer.h"
#include "WorleyNoise.h"
#include "FullscreenQuad.h"
#include "Voxelizer.h"
#include "VoxelDebug.h"
#include "FloodFill.h"

//---------------------------------------------------------------------
// Window
//---------------------------------------------------------------------
static unsigned int winWidth  = 800;
static unsigned int winHeight = 600;

//---------------------------------------------------------------------
// Scene-level globals (need to be accessible from GLFW callbacks)
//---------------------------------------------------------------------
static OrbitCamera    g_camera;
static Voxelizer*     g_voxelizer = nullptr;
static VoxelFloodFill* g_floodFill = nullptr;
static NoiseDebugView g_noiseView;

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

    // Noise debug toggle and slice controls
    if (key == GLFW_KEY_N && action == GLFW_PRESS) {
        g_noiseView.enabled = !g_noiseView.enabled;
        std::cout << "Noise debug: " << (g_noiseView.enabled ? "ON" : "OFF") << "\n";
    }
    if (key == GLFW_KEY_UP   && (action == GLFW_PRESS || action == GLFW_REPEAT))
        g_noiseView.sliceZ = glm::min(g_noiseView.sliceZ + 0.02f, 1.0f);
    if (key == GLFW_KEY_DOWN && (action == GLFW_PRESS || action == GLFW_REPEAT))
        g_noiseView.sliceZ = glm::max(g_noiseView.sliceZ - 0.02f, 0.0f);

    // Space: detonate smoke grenade in arena corner
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
        if (g_floodFill && g_voxelizer) {
            glm::vec3 seedPos = glm::vec3(
                g_voxelizer->boundsMin.x + g_voxelizer->voxelSize * 5.0f,
                g_voxelizer->boundsMin.y + g_voxelizer->voxelSize * 2.0f,
                g_voxelizer->boundsMin.z + g_voxelizer->voxelSize * 5.0f
            );
            g_floodFill->seed(seedPos, g_voxelizer->gridSize,
                              g_voxelizer->boundsMin, g_voxelizer->voxelSize);
        }
    }
}

static void mouse_button_callback(GLFWwindow*, int button, int action, int) {
    g_camera.onMouseButton(button, action);
}

static void cursor_pos_callback(GLFWwindow* window, double x, double y) {
    g_camera.onMouseMove(window, (float)x, (float)y);
}

static void scroll_callback(GLFWwindow*, double, double dy) {
    g_camera.onScroll((float)dy);
}

//---------------------------------------------------------------------
// main
//---------------------------------------------------------------------
int main()
{
    // --- Window + context ---
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

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

    glEnable(GL_DEPTH_TEST);

    // --- Worley noise ---
    WorleyNoise worleyNoise;
    worleyNoise.init(128);

    FullscreenQuad fsQuad;
    fsQuad.init();

    g_noiseView.init();

    // --- Voxel scene (procedural test arena) ---
    Voxelizer voxelizer;
    voxelizer.generateTestScene(0.15f, 64);

    VoxelDebug voxelDebug;
    voxelDebug.init();

    // --- Flood fill ---
    VoxelFloodFill floodFill;
    floodFill.init(voxelizer.totalVoxels);

    g_voxelizer = &voxelizer;
    g_floodFill = &floodFill;

    // --- Timing ---
    float lastFrameTime = 0.0f;

    // --- Render loop ---
    while (!glfwWindowShouldClose(window))
    {
        float time = (float)glfwGetTime();
        float dt   = time - lastFrameTime;
        lastFrameTime = time;

        glfwPollEvents();

        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // GPU simulation
        worleyNoise.generate(time);
        floodFill.propagate(4, voxelizer.gridSize, voxelizer.boundsMin,
                            voxelizer.voxelSize, voxelizer.staticVoxels, dt);

        if (g_noiseView.enabled) {
            g_noiseView.draw(worleyNoise.texture, fsQuad);
        } else {
            float aspect = (float)winWidth / (float)winHeight;
            glm::mat4 view = g_camera.view();
            glm::mat4 proj = g_camera.proj(aspect);

            voxelDebug.drawWithSmoke(voxelizer.staticVoxels, floodFill.currentBuffer(),
                                     view, proj,
                                     voxelizer.gridSize, voxelizer.boundsMin,
                                     voxelizer.voxelSize);
        }

        glfwSwapBuffers(window);
    }

    // --- Cleanup ---
    g_voxelizer = nullptr;
    g_floodFill = nullptr;

    floodFill.destroy();
    voxelizer.destroy();
    voxelDebug.destroy();
    worleyNoise.destroy();
    fsQuad.destroy();
    g_noiseView.destroy();

    glfwTerminate();
    return 0;
}

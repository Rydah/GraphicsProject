////////////////////////////////////////////////////////////////////////
//
//  SUTD Course 50.017 - CS2 Volumetric Smoke (OpenGL 4.3)
//
////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <sstream>
#include <vector>
#include <fstream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include "glVersion.h"
#include "shaderSource.h"
#include "shader.h"
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

using namespace std;

///  OpenGL debug callback  ///
void APIENTRY glDebugOutput(GLenum source, GLenum type, unsigned int id,
                            GLenum severity, GLsizei length,
                            const char* message, const void* userParam)
{
    // Ignore non-significant codes
    if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;

    std::cout << "GL DEBUG [" << id << "]: " << message << std::endl;

    switch (source) {
        case GL_DEBUG_SOURCE_API:             std::cout << "  Source: API"; break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   std::cout << "  Source: Window System"; break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: std::cout << "  Source: Shader Compiler"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:     std::cout << "  Source: Third Party"; break;
        case GL_DEBUG_SOURCE_APPLICATION:      std::cout << "  Source: Application"; break;
        case GL_DEBUG_SOURCE_OTHER:           std::cout << "  Source: Other"; break;
    }
    std::cout << std::endl;

    switch (type) {
        case GL_DEBUG_TYPE_ERROR:               std::cout << "  Type: Error"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: std::cout << "  Type: Deprecated"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  std::cout << "  Type: Undefined Behavior"; break;
        case GL_DEBUG_TYPE_PORTABILITY:         std::cout << "  Type: Portability"; break;
        case GL_DEBUG_TYPE_PERFORMANCE:         std::cout << "  Type: Performance"; break;
        case GL_DEBUG_TYPE_MARKER:              std::cout << "  Type: Marker"; break;
        case GL_DEBUG_TYPE_OTHER:               std::cout << "  Type: Other"; break;
    }
    std::cout << std::endl;

    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:         std::cout << "  Severity: HIGH"; break;
        case GL_DEBUG_SEVERITY_MEDIUM:       std::cout << "  Severity: Medium"; break;
        case GL_DEBUG_SEVERITY_LOW:          std::cout << "  Severity: Low"; break;
        case GL_DEBUG_SEVERITY_NOTIFICATION: std::cout << "  Severity: Notification"; break;
    }
    std::cout << "\n" << std::endl;
}


#define MAX_BUFFER_SIZE            1024

#define _ROTATE_FACTOR              0.005f
#define _SCALE_FACTOR               0.01f
#define _TRANS_FACTOR               0.02f

#define _Z_NEAR                     0.001f
#define _Z_FAR                      100.0f



/***********************************************************************/
/**************************   global variables   ***********************/
/***********************************************************************/


// Window size
unsigned int winWidth  = 800;
unsigned int winHeight = 600;

// Orbit camera
float camYaw = 45.0f;       // horizontal angle (degrees)
float camPitch = 35.0f;     // vertical angle (degrees)
float camDist = 18.0f;      // distance from target
glm::vec3 camera_target = glm::vec3(0.0f, 2.0f, 0.0f);
glm::vec3 camera_up = glm::vec3(0.0f, 1.0f, 0.0f);
float camera_fovy = 45.0f;
glm::mat4 projection;

glm::vec3 getCameraPosition() {
    float yawRad = glm::radians(camYaw);
    float pitchRad = glm::radians(camPitch);
    float x = camDist * cos(pitchRad) * sin(yawRad);
    float y = camDist * sin(pitchRad);
    float z = camDist * cos(pitchRad) * cos(yawRad);
    return camera_target + glm::vec3(x, y, z);
}

// Mouse interaction
bool leftMouseButtonHold = false;
bool middleMouseButtonHold = false;
bool isFirstMouse = true;
float prevMouseX;
float prevMouseY;

// Debug visualization
bool showNoiseDebug = false;
float noiseSliceZ = 0.5f;
bool showVoxelDebug = true;

// Flood fill (global pointers for key_callback access)
Voxelizer* g_voxelizer = nullptr;
VoxelFloodFill* g_floodFill = nullptr;

// Timing
float deltaTime = 0.0f;
float lastFrameTime = 0.0f;


// declaration
void processInput(GLFWwindow *window);
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);





/******************************************************************************/
/***************                  Callback Function              **************/
/******************************************************************************/

// process all input: query GLFW whether relevant keys are pressed/released this frame and react accordingly
// ---------------------------------------------------------------------------------------------------------
void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}


// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // make sure the viewport matches the new window dimensions; note that width and
    // height will be significantly larger than specified on retina displays.

    glViewport(0, 0, width, height);

    winWidth  = width;
    winHeight = height;
}


// glfw: whenever a key is pressed, this callback is called
// ----------------------------------------------------------------------
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_N && action == GLFW_PRESS)
    {
        showNoiseDebug = !showNoiseDebug;
        std::cout << "Noise debug: " << (showNoiseDebug ? "ON" : "OFF") << std::endl;
    }
    if (key == GLFW_KEY_UP && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        noiseSliceZ = glm::min(noiseSliceZ + 0.02f, 1.0f);
    }
    if (key == GLFW_KEY_DOWN && (action == GLFW_PRESS || action == GLFW_REPEAT))
    {
        noiseSliceZ = glm::max(noiseSliceZ - 0.02f, 0.0f);
    }
    if (key == GLFW_KEY_V && action == GLFW_PRESS)
    {
        showVoxelDebug = !showVoxelDebug;
        std::cout << "Voxel debug: " << (showVoxelDebug ? "ON" : "OFF") << std::endl;
    }
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    {
        if (g_floodFill && g_voxelizer) {
            // Seed in corner of first room, near the floor
            glm::vec3 seedPos = glm::vec3(
                g_voxelizer->boundsMin.x + g_voxelizer->voxelSize * 5.0f,   // near -x wall
                g_voxelizer->boundsMin.y + g_voxelizer->voxelSize * 2.0f,   // just above floor
                g_voxelizer->boundsMin.z + g_voxelizer->voxelSize * 5.0f    // near -z wall
            );
            g_floodFill->seed(seedPos, g_voxelizer->gridSize, g_voxelizer->boundsMin, g_voxelizer->voxelSize);
        }
    }
}


void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        leftMouseButtonHold = (action == GLFW_PRESS);
        if (action == GLFW_PRESS) isFirstMouse = true;
    }
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        middleMouseButtonHold = (action == GLFW_PRESS);
        if (action == GLFW_PRESS) isFirstMouse = true;
    }
}


void cursor_pos_callback(GLFWwindow* window, double mouseX, double mouseY)
{
    if (leftMouseButtonHold || middleMouseButtonHold)
    {
        if (isFirstMouse) {
            prevMouseX = (float)mouseX;
            prevMouseY = (float)mouseY;
            isFirstMouse = false;
            return;
        }

        float dx = (float)(mouseX - prevMouseX);
        float dy = (float)(mouseY - prevMouseY);
        prevMouseX = (float)mouseX;
        prevMouseY = (float)mouseY;

        if (leftMouseButtonHold && glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            // Shift+drag: pan the target
            glm::vec3 camPos = getCameraPosition();
            glm::vec3 forward = glm::normalize(camera_target - camPos);
            glm::vec3 right = glm::normalize(glm::cross(forward, camera_up));
            glm::vec3 up = glm::normalize(glm::cross(right, forward));
            float panSpeed = camDist * 0.002f;
            camera_target -= right * dx * panSpeed;
            camera_target += up * dy * panSpeed;
        }
        else if (leftMouseButtonHold) {
            // Left drag: orbit rotation
            camYaw -= dx * 0.3f;
            camPitch += dy * 0.3f;
            camPitch = glm::clamp(camPitch, -89.0f, 89.0f);
        }
    }
}


void scroll_callback(GLFWwindow* window, double xOffset, double yOffset)
{
    camDist -= (float)yOffset * 1.0f;
    camDist = glm::clamp(camDist, 2.0f, 50.0f);
}




/******************************************************************************/
/***************                    Main Function                **************/
/******************************************************************************/

int main()
{
    // glfw: initialize and configure
    // ------------------------------
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
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetScrollCallback(window, scroll_callback);

    // tell GLFW to capture our mouse
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    // glad: load all OpenGL function pointers
    // ---------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // Enable OpenGL debug output
    int flags;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugOutput, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        std::cout << "OpenGL debug output enabled" << std::endl;
    }

    // Print OpenGL info and compute capabilities
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;

    int workGroupCount[3], workGroupSize[3], workGroupInvocations;
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &workGroupCount[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &workGroupCount[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &workGroupCount[2]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &workGroupSize[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &workGroupSize[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &workGroupSize[2]);
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &workGroupInvocations);
    std::cout << "Max compute work group count: " << workGroupCount[0] << ", " << workGroupCount[1] << ", " << workGroupCount[2] << std::endl;
    std::cout << "Max compute work group size:  " << workGroupSize[0] << ", " << workGroupSize[1] << ", " << workGroupSize[2] << std::endl;
    std::cout << "Max compute invocations:      " << workGroupInvocations << std::endl;

    // --- Step 2/3 verification: compute shader writes to SSBO, CPU reads back ---
    {
        const char* testComputeSrc =
        GLSL_VERSION_CORE
        "layout(local_size_x = 64) in;\n"
            "layout(std430, binding = 0) buffer OutBuf { int data[]; };\n"
            "void main() {\n"
            "    uint idx = gl_GlobalInvocationID.x;\n"
            "    data[idx] = int(idx * idx);\n"
            "}\n";

        ComputeShader testCS;
        testCS.setUp(testComputeSrc);

        const int N = 256;
        SSBOBuffer testBuf;
        testBuf.allocate(N * sizeof(int));
        testBuf.clear();
        testBuf.bindBase(0);

        testCS.dispatch(N);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        std::vector<int> result = testBuf.download<int>(N);
        bool pass = true;
        for (int i = 0; i < N; i++) {
            if (result[i] != i * i) { pass = false; break; }
        }
        std::cout << "Compute shader SSBO test: " << (pass ? "PASSED" : "FAILED") << std::endl;

        testBuf.destroy();
        glDeleteProgram(testCS.ID);
    }

    // --- Step 4 verification: imageStore round-trip on 64^3 R16F texture ---
    {
        const char* texComputeSrc =
            GLSL_VERSION_CORE
            "layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;\n"
            "layout(binding = 0, r16f) uniform image3D u_Volume;\n"
            "void main() {\n"
            "    ivec3 coord = ivec3(gl_GlobalInvocationID);\n"
            "    ivec3 size = imageSize(u_Volume);\n"
            "    if (any(greaterThanEqual(coord, size))) return;\n"
            "    float val = float(coord.x + coord.y + coord.z) / float(size.x + size.y + size.z);\n"
            "    imageStore(u_Volume, coord, vec4(val));\n"
            "}\n";

        const char* readbackSrc =
            GLSL_VERSION_CORE
            "layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;\n"
            "layout(binding = 0, r16f) readonly uniform image3D u_Volume;\n"
            "layout(std430, binding = 0) buffer OutBuf { float data[]; };\n"
            "uniform ivec3 u_Size;\n"
            "void main() {\n"
            "    ivec3 coord = ivec3(gl_GlobalInvocationID);\n"
            "    if (any(greaterThanEqual(coord, u_Size))) return;\n"
            "    int idx = coord.x + coord.y * u_Size.x + coord.z * u_Size.x * u_Size.y;\n"
            "    data[idx] = imageLoad(u_Volume, coord).r;\n"
            "}\n";

        const int SZ = 64;
        Texture3D testTex;
        testTex.create(SZ, SZ, SZ, GL_R16F);

        // Write pass
        ComputeShader writeCS;
        writeCS.setUp(texComputeSrc);
        testTex.bindImage(0, GL_WRITE_ONLY);
        writeCS.dispatch(SZ, SZ, SZ);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        // Readback pass into SSBO
        ComputeShader readCS;
        readCS.setUp(readbackSrc);
        readCS.use();
        readCS.setIVec3("u_Size", glm::ivec3(SZ));

        int totalVoxels = SZ * SZ * SZ;
        SSBOBuffer readBuf;
        readBuf.allocate(totalVoxels * sizeof(float));
        readBuf.bindBase(0);
        testTex.bindImage(0, GL_READ_ONLY);

        readCS.dispatch(SZ, SZ, SZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        std::vector<float> result = readBuf.download<float>(totalVoxels);
        bool pass = true;
        float maxSum = float(SZ + SZ + SZ);
        // Spot-check a few voxels
        int checks[][3] = {{0,0,0}, {1,2,3}, {32,32,32}, {63,63,63}};
        for (auto& c : checks) {
            int idx = c[0] + c[1]*SZ + c[2]*SZ*SZ;
            float expected = float(c[0]+c[1]+c[2]) / maxSum;
            if (std::abs(result[idx] - expected) > 0.01f) { pass = false; break; }
        }
        std::cout << "Texture3D imageStore test:    " << (pass ? "PASSED" : "FAILED") << std::endl;

        readBuf.destroy();
        testTex.destroy();
        glDeleteProgram(writeCS.ID);
        glDeleteProgram(readCS.ID);
    }

    // configure global opengl state
    // -----------------------------
    glEnable(GL_DEPTH_TEST);

    // --- Worley noise + visualization setup ---
    WorleyNoise worleyNoise;
    worleyNoise.init(128);

    FullscreenQuad fsQuad;
    fsQuad.init();

    // Noise slice visualization shader
    const char* noiseVisVS =
        GLSL_VERSION
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aUV;\n"
        "out vec2 vUV;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    vUV = aUV;\n"
        "}\n";

    const char* noiseVisFS =
        GLSL_VERSION
        "in vec2 vUV;\n"
        "out vec4 FragColor;\n"
        "uniform sampler3D u_NoiseTex;\n"
        "uniform float u_SliceZ;\n"
        "void main() {\n"
        "    float n = texture(u_NoiseTex, vec3(vUV, u_SliceZ)).r;\n"
        "    FragColor = vec4(n, n, n, 1.0);\n"
        "}\n";

    shader noiseVisShader;
    noiseVisShader.setUpShader(noiseVisVS, noiseVisFS);

    // --- Voxelizer setup (procedural test scene) ---
    Voxelizer voxelizer;
    voxelizer.generateTestScene(0.15f, 64);

    VoxelDebug voxelDebug;
    voxelDebug.init();

    // --- Flood fill setup ---
    VoxelFloodFill floodFill;
    floodFill.init(voxelizer.totalVoxels);
    g_voxelizer = &voxelizer;
    g_floodFill = &floodFill;

    // render loop
    // -----------
    while (!glfwWindowShouldClose(window))
    {
        // input
        // -----
        processInput(window);

        // render
        // ------
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Timing
        float time = (float)glfwGetTime();
        deltaTime = time - lastFrameTime;
        lastFrameTime = time;

        // Generate Worley noise each frame
        worleyNoise.generate(time);

        // Propagate flood fill
        floodFill.propagate(4, voxelizer.gridSize, voxelizer.boundsMin,
                            voxelizer.voxelSize, voxelizer.staticVoxels, deltaTime);

        if (showNoiseDebug)
        {
            // Draw noise slice as fullscreen quad
            glDisable(GL_DEPTH_TEST);
            noiseVisShader.use();
            noiseVisShader.setInt("u_NoiseTex", 0);
            noiseVisShader.setFloat("u_SliceZ", noiseSliceZ);
            worleyNoise.texture.bindSampler(0);
            fsQuad.draw();
            glEnable(GL_DEPTH_TEST);
        }
        else
        {
            // Voxel scene rendering
            glm::vec3 camPos = getCameraPosition();
            projection = glm::perspective(glm::radians(camera_fovy), (float)winWidth / (float)winHeight, _Z_NEAR, _Z_FAR);
            glm::mat4 view = glm::lookAt(camPos, camera_target, camera_up);

            // Always draw voxels (walls + smoke)
            voxelDebug.drawWithSmoke(voxelizer.staticVoxels, floodFill.currentBuffer(),
                                     view, projection,
                                     voxelizer.gridSize, voxelizer.boundsMin, voxelizer.voxelSize);
        }


        // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
        // -------------------------------------------------------------------------------
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // optional: de-allocate all resources once they've outlived their purpose:
    // ------------------------------------------------------------------------
    g_voxelizer = nullptr;
    g_floodFill = nullptr;
    floodFill.destroy();
    voxelizer.destroy();
    voxelDebug.destroy();
    worleyNoise.destroy();
    fsQuad.destroy();
    glDeleteProgram(noiseVisShader.ID);

    // glfw: terminate, clearing all previously allocated GLFW resources.
    // ------------------------------------------------------------------
    glfwTerminate();
    return 0;
}


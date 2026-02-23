#ifndef GL_DEBUG_H
#define GL_DEBUG_H

#include <glad/glad.h>
#include <iostream>

// OpenGL 4.3 debug message callback.
// Register with:
//   glEnable(GL_DEBUG_OUTPUT);
//   glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
//   glDebugMessageCallback(glDebugOutput, nullptr);
//   glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
void APIENTRY glDebugOutput(GLenum source, GLenum type, unsigned int id,
                            GLenum severity, GLsizei /*length*/,
                            const char* message, const void* /*userParam*/)
{
    // Suppress driver noise: buffer detail, shader recompile, perf hints
    if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;

    std::cout << "GL DEBUG [" << id << "]: " << message << "\n";

    switch (source) {
        case GL_DEBUG_SOURCE_API:             std::cout << "  Source: API"; break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   std::cout << "  Source: Window System"; break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: std::cout << "  Source: Shader Compiler"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:     std::cout << "  Source: Third Party"; break;
        case GL_DEBUG_SOURCE_APPLICATION:     std::cout << "  Source: Application"; break;
        default:                              std::cout << "  Source: Other"; break;
    }
    std::cout << "\n";

    switch (type) {
        case GL_DEBUG_TYPE_ERROR:               std::cout << "  Type: Error"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: std::cout << "  Type: Deprecated"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  std::cout << "  Type: Undefined Behavior"; break;
        case GL_DEBUG_TYPE_PORTABILITY:         std::cout << "  Type: Portability"; break;
        case GL_DEBUG_TYPE_PERFORMANCE:         std::cout << "  Type: Performance"; break;
        case GL_DEBUG_TYPE_MARKER:              std::cout << "  Type: Marker"; break;
        default:                               std::cout << "  Type: Other"; break;
    }
    std::cout << "\n";

    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:         std::cout << "  Severity: HIGH"; break;
        case GL_DEBUG_SEVERITY_MEDIUM:       std::cout << "  Severity: Medium"; break;
        case GL_DEBUG_SEVERITY_LOW:          std::cout << "  Severity: Low"; break;
        case GL_DEBUG_SEVERITY_NOTIFICATION: std::cout << "  Severity: Notification"; break;
    }
    std::cout << "\n" << std::endl;
}

// Call once after GLAD is loaded to enable debug output.
inline void enableGLDebug() {
    int flags;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugOutput, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        std::cout << "OpenGL debug output enabled\n";
    }
}

// Print GPU info and compute limits to stdout.
inline void printGPUInfo() {
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << "\n";
    std::cout << "Renderer:       " << glGetString(GL_RENDERER) << "\n";

    int wgCount[3], wgSize[3], wgInvoc;
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &wgCount[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &wgCount[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &wgCount[2]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE,  0, &wgSize[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE,  1, &wgSize[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE,  2, &wgSize[2]);
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &wgInvoc);
    std::cout << "Max WG count:  " << wgCount[0] << " x " << wgCount[1] << " x " << wgCount[2] << "\n";
    std::cout << "Max WG size:   " << wgSize[0]  << " x " << wgSize[1]  << " x " << wgSize[2]  << "\n";
    std::cout << "Max WG invoc:  " << wgInvoc << "\n";
}

#endif // GL_DEBUG_H

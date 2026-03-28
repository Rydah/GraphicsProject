cmake_minimum_required(VERSION 3.20)
project(GraphicsProject CXX C)

set(CMAKE_CXX_STANDARD 20)

find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(GraphicsProject
    src/main.cpp
    src/glad.c
    src/core/smokeField.cpp
    src/Procedural/FloodFillToSmoke.cpp
    src/Procedural/ProceduralSmokeSystem.cpp
    src/SmokeSolver/SmokeSolver.cpp
    src/SmokeSolver/ApplyForces.cpp
    src/SmokeSolver/AdvectSmoke.cpp
    src/SmokeSolver/AdvectVelocity.cpp
    src/SmokeSolver/ComputeDivergence.cpp
    src/SmokeSolver/PressureJacobi.cpp
    src/SmokeSolver/ProjectVelocity.cpp
    # Dear ImGui
    includes/imgui/imgui.cpp
    includes/imgui/imgui_draw.cpp
    includes/imgui/imgui_tables.cpp
    includes/imgui/imgui_widgets.cpp
    includes/imgui/backends/imgui_impl_glfw.cpp
    includes/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(GraphicsProject PRIVATE
    includes/
    includes/imgui
    includes/imgui/backends
    src/
)

if(APPLE)
    # macOS-specific libraries
    target_link_libraries(GraphicsProject
        glfw
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
elseif(UNIX)
    # Linux-specific libraries
    target_link_libraries(GraphicsProject
        glfw
        OpenGL::GL
    )
elseif(WIN32)
    # Windows-specific libraries
    target_link_libraries(GraphicsProject
        glfw
        opengl32
    )
endif()

set_source_files_properties(src/glad.c PROPERTIES LANGUAGE C)
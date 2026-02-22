cmake_minimum_required(VERSION 3.20)
project(GraphicsProject CXX C)

set(CMAKE_CXX_STANDARD 20)

find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(GraphicsProject
    src/main.cpp
    src/glad.c
)

target_include_directories(GraphicsProject PRIVATE
    includes/
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
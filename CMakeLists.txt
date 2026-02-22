cmake_minimum_required(VERSION 3.20)
project(GraphicsProject CXX C)
set(CMAKE_CXX_STANDARD 20)

# Find packages installed via Homebrew
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

add_executable(GraphicsProject
    src/main.cpp
    src/glad.c          
)

target_include_directories(GraphicsProject PRIVATE
    includes/
    src/
)

target_link_libraries(GraphicsProject
    glfw
    "-framework OpenGL"
    "-framework Cocoa"
    "-framework IOKit"
    "-framework CoreVideo"
)

set_source_files_properties(src/glad.c PROPERTIES LANGUAGE C)
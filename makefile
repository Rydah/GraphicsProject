TARGET      := GraphicsProject
BUILD_DIR   := build
SRC_DIR     := src
INC_DIR     := includes
LIB_DIR     := libs
IMGUI_DIR   := $(INC_DIR)/imgui

CXX         := g++
CC          := gcc

TARGET_BIN  := $(BUILD_DIR)/$(TARGET).exe

# ------------------------------------------------------------------
# Project sources
# ------------------------------------------------------------------
CPP_SOURCES := \
	$(wildcard $(SRC_DIR)/*.cpp) \
	$(wildcard $(SRC_DIR)/core/*.cpp) \
	$(wildcard $(SRC_DIR)/camera/*.cpp) \
	$(wildcard $(SRC_DIR)/Debugtest/*.cpp) \
	$(wildcard $(SRC_DIR)/Procedural/*.cpp) \
	$(wildcard $(SRC_DIR)/Rendering/*.cpp) \
	$(wildcard $(SRC_DIR)/SmokeSolver/*.cpp) \
	$(wildcard $(SRC_DIR)/Voxel/*.cpp)

C_SOURCES := \
	$(SRC_DIR)/glad.c

# ------------------------------------------------------------------
# Dear ImGui sources
# ------------------------------------------------------------------
IMGUI_SOURCES := \
	$(IMGUI_DIR)/imgui.cpp \
	$(IMGUI_DIR)/imgui_draw.cpp \
	$(IMGUI_DIR)/imgui_tables.cpp \
	$(IMGUI_DIR)/imgui_widgets.cpp \
	$(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
	$(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp

CPP_SOURCES += $(IMGUI_SOURCES)

# ------------------------------------------------------------------
# Object paths
# This maps:
#   src/foo.cpp                  -> build/src/foo.o
#   includes/imgui/imgui.cpp     -> build/includes/imgui/imgui.o
# ------------------------------------------------------------------
CPP_OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CPP_SOURCES))
C_OBJECTS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
OBJECTS     := $(CPP_OBJECTS) $(C_OBJECTS)

# ------------------------------------------------------------------
# Include paths
# -Iincludes/imgui is important because imgui backend headers include
# "imgui.h" internally, not "imgui/imgui.h"
# ------------------------------------------------------------------
CXXFLAGS := -std=c++17 -Wall -Wextra -I$(SRC_DIR) -I$(INC_DIR) -I$(IMGUI_DIR)
CFLAGS   := -Wall -Wextra -I$(SRC_DIR) -I$(INC_DIR) -I$(IMGUI_DIR)

LDFLAGS := -L$(LIB_DIR)
LDLIBS  := -lglfw3 -lopengl32 -lgdi32

# ------------------------------------------------------------------
# Main targets
# ------------------------------------------------------------------
all: $(TARGET_BIN)

$(TARGET_BIN): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS) $(LDLIBS)

# ------------------------------------------------------------------
# Compile C++ files from anywhere in the tree
# ------------------------------------------------------------------
$(BUILD_DIR)/%.o: %.cpp
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ------------------------------------------------------------------
# Compile C files from anywhere in the tree
# ------------------------------------------------------------------
$(BUILD_DIR)/%.o: %.c
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	if exist $(BUILD_DIR) rmdir /S /Q $(BUILD_DIR)

rebuild: clean all

run: all
	$(TARGET_BIN)

print:
	@echo CPP_SOURCES=$(CPP_SOURCES)
	@echo C_SOURCES=$(C_SOURCES)
	@echo OBJECTS=$(OBJECTS)

.PHONY: all clean rebuild run print
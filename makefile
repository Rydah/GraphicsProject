TARGET      := GraphicsProject
BUILD_DIR   := build
SRC_DIR     := src
INC_DIR     := includes
LIB_DIR     := libs

CXX         := g++
CC          := gcc

TARGET_BIN  := $(BUILD_DIR)/$(TARGET).exe

CPP_SOURCES := \
	$(wildcard $(SRC_DIR)/*.cpp) \
	$(wildcard $(SRC_DIR)/core/*.cpp) \
	$(wildcard $(SRC_DIR)/camera/*.cpp) \
	$(wildcard $(SRC_DIR)/Debugtest/*.cpp) \
	$(wildcard $(SRC_DIR)/Procedural/*.cpp) \
	$(wildcard $(SRC_DIR)/Rendering/*.cpp) \
	$(wildcard $(SRC_DIR)/SmokeSolver/*.cpp) \
	$(wildcard $(SRC_DIR)/Voxel/*.cpp)

C_SOURCES   := $(SRC_DIR)/glad.c

CPP_OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(CPP_SOURCES))
C_OBJECTS   := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
OBJECTS     := $(CPP_OBJECTS) $(C_OBJECTS)

CXXFLAGS := -std=c++17 -Wall -Wextra -I$(SRC_DIR) -I$(INC_DIR)
CFLAGS   := -Wall -Wextra -I$(SRC_DIR) -I$(INC_DIR)

LDFLAGS  := -L$(LIB_DIR)
LDLIBS   := -lglfw3 -lopengl32 -lgdi32

all: $(TARGET_BIN)

$(TARGET_BIN): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR) $(BUILD_DIR)/core $(BUILD_DIR)/camera $(BUILD_DIR)/Debugtest $(BUILD_DIR)/Procedural $(BUILD_DIR)/Rendering $(BUILD_DIR)/SmokeSolver $(BUILD_DIR)/Voxel
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)

$(BUILD_DIR)/core:
	if not exist $(BUILD_DIR)\core mkdir $(BUILD_DIR)\core

$(BUILD_DIR)/camera:
	if not exist $(BUILD_DIR)\camera mkdir $(BUILD_DIR)\camera

$(BUILD_DIR)/Debugtest:
	if not exist $(BUILD_DIR)\Debugtest mkdir $(BUILD_DIR)\Debugtest

$(BUILD_DIR)/Procedural:
	if not exist $(BUILD_DIR)\Procedural mkdir $(BUILD_DIR)\Procedural

$(BUILD_DIR)/Rendering:
	if not exist $(BUILD_DIR)\Rendering mkdir $(BUILD_DIR)\Rendering

$(BUILD_DIR)/SmokeSolver:
	if not exist $(BUILD_DIR)\SmokeSolver mkdir $(BUILD_DIR)\SmokeSolver

$(BUILD_DIR)/Voxel:
	if not exist $(BUILD_DIR)\Voxel mkdir $(BUILD_DIR)\Voxel

clean:
	if exist $(BUILD_DIR) rmdir /S /Q $(BUILD_DIR)

rebuild: clean all

run: all
	$(TARGET_BIN)

.PHONY: all clean rebuild run
TARGET      := GraphicsProject
BUILD_DIR   := build
SRC_DIR     := src
INC_DIR     := includes
LIB_DIR     := libs

CXX         := g++
CC          := gcc

TARGET_BIN  := $(BUILD_DIR)/$(TARGET).exe

CPP_SOURCES := $(SRC_DIR)/main.cpp
C_SOURCES   := $(SRC_DIR)/glad.c

CPP_OBJECTS := $(BUILD_DIR)/main.o
C_OBJECTS   := $(BUILD_DIR)/glad.o
OBJECTS     := $(CPP_OBJECTS) $(C_OBJECTS)

CXXFLAGS := -std=c++17 -Wall -Wextra -I$(SRC_DIR) -I$(INC_DIR)
CFLAGS   := -Wall -Wextra -I$(SRC_DIR) -I$(INC_DIR)

LDFLAGS  := -L$(LIB_DIR)
LDLIBS   := -lglfw3 -lopengl32 -lgdi32

all: $(TARGET_BIN)

$(TARGET_BIN): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/main.o: $(SRC_DIR)/main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/glad.o: $(SRC_DIR)/glad.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)

clean:
	if exist $(BUILD_DIR) rmdir /S /Q $(BUILD_DIR)

rebuild: clean all

run: all
	$(TARGET_BIN)

.PHONY: all clean rebuild run
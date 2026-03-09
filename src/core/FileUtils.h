#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>

inline std::string loadTextFile(const std::string& path) {
    std::ifstream file(path);

    if (!file.is_open()) {
        throw std::runtime_error("[TEXTFILE LOADER] Failed to open file: " + path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}
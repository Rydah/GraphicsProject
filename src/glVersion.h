#pragma once
#ifdef __APPLE__
    #define GLSL_VERSION_CORE "#version 410 core\n"
    #define GLSL_VERSION      "#version 410\n"
#else
    #define GLSL_VERSION_CORE "#version 430 core\n"
    #define GLSL_VERSION      "#version 430\n"
#endif
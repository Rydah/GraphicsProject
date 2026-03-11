#pragma once 

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <string>
#include "core/smokeField.h"
#include "core/Buffer.h"
#include "core/ComputeShader.h"

class ComputeDivergence {
    public:
    void init();
    void run(const VoxelDomain& domain,
               const SSBOBuffer& velocityBuf,
               const SSBOBuffer& wallBuf,
               const SSBOBuffer& divergenceBuf);
    void destroy();

    private:
    ComputeShader shader_;

};
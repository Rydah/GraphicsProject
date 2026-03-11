#include <iostream>
#include "core/smokeField.h"

void SmokeField::init(const VoxelDomain& domain) {
    this->domain = domain;

    pressure1Curr = true;
    velocity1Curr = true;
    density1Curr = true;

    const size_t scalarBytes = static_cast<size_t>(domain.totalVoxels) * sizeof(float);
    const size_t velocityBytes = static_cast<size_t>(domain.totalVoxels) * sizeof(glm::vec4);

    density1.allocate(scalarBytes);
    density2.allocate(scalarBytes);

    pressure1.allocate(scalarBytes);
    pressure2.allocate(scalarBytes);

    divergence.allocate(scalarBytes);

    velocity1.allocate(velocityBytes);
    velocity2.allocate(velocityBytes);

    clear(); // ensure everything is set to zero

    std::cout << "[SmokeField] Initialised for Voxel Grid: " 
              << domain.gridSize.x << "x"
              << domain.gridSize.y << "x"
              << domain.gridSize.z << "("
              << domain.totalVoxels << " Total Voxels)" << std::endl;

}

SSBOBuffer& SmokeField::getSrcPressure() {
    return pressure1Curr ? pressure1 : pressure2;
}

SSBOBuffer& SmokeField::getDestPressure() {
    return pressure1Curr ? pressure2 : pressure1;
}

void SmokeField::swapPressure() {
    pressure1Curr = !pressure1Curr;
}

SSBOBuffer& SmokeField::getSrcVelocity() {
    return velocity1Curr ? velocity1 : velocity2;
}

SSBOBuffer& SmokeField::getDestVelocity() {
    return velocity1Curr ? velocity2 : velocity1;
}

void SmokeField::swapVelocity() {
    velocity1Curr = !velocity1Curr;
}

SSBOBuffer& SmokeField::getSrcDensity() {
    return density1Curr ? density1 : density2;
}

SSBOBuffer& SmokeField::getDestDensity() {
    return density1Curr ? density2 : density1;
}

void SmokeField::swapDensity() {
    density1Curr = !density1Curr;
}

void SmokeField::clear() {

    density1.clear();
    density2.clear();

    velocity1.clear();
    velocity2.clear();

    pressure1.clear();
    pressure2.clear();

    divergence.clear();

    std::cout << "[SmokeField] Cleared all buffers." << std::endl;
}

void SmokeField::destroy() {
    density1.destroy();
    density2.destroy();

    velocity1.destroy();
    velocity2.destroy();

    pressure1.destroy();
    pressure2.destroy();

    divergence.destroy();

    std::cout << "[SmokeField] Destroyed all buffers." << std::endl;
}
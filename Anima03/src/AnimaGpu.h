#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace anima02 {
struct GpuStatus {
    bool available = false;
    bool dxrSupported = false;
    std::string adapter = "Unavailable";
    std::string backend = "No native GPU backend";
    std::size_t dedicatedMemoryMB = 0;
};

GpuStatus QueryGpu();
std::vector<double> ComputeSquaresOnGpu(std::size_t count);
}

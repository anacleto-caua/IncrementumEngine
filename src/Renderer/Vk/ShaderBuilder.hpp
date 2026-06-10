#pragma once

#include <vector>
#include <string>

#include <vulkan/vulkan.h>

VkPipelineShaderStageCreateInfo CreateShaderStage(
    VkShaderStageFlagBits stage,
    std::string filename,
    std::vector<char> &shader_code
);

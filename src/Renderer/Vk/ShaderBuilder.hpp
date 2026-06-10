#pragma once

#include <vector>
#include <string>

#include <vulkan/vulkan.h>

VkPipelineShaderStageCreateInfo CreateShaderStage(
    VkShaderStageFlagBits stage,
    std::string filename,
    std::vector<u32> &shader_code
);

#pragma once

#include <vector>
#include <string>

#include <vulkan/vulkan.h>

#include <Utils/IO.hpp>

namespace ShaderBuilder {
    static inline VkPipelineShaderStageCreateInfo CreateShaderStage(
            VkShaderStageFlagBits Stage,
            std::string Filename,
            std::vector<char> &ShaderCode,
            VkDevice Device)
    {
        uint32_t ShaderSize;
        IO::BinaryRead(Filename, ShaderCode, ShaderSize);

        VkShaderModuleCreateInfo ShaderModuleCreateInfo{};
        ShaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ShaderModuleCreateInfo.codeSize = ShaderSize;
        ShaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(ShaderCode.data());

        VkShaderModule ShaderModule{};
        if (vkCreateShaderModule(Device, &ShaderModuleCreateInfo, nullptr, &ShaderModule) != VK_SUCCESS) {
            throw std::runtime_error("Shader Module creation failed for shader: " + Filename);
        }

        VkPipelineShaderStageCreateInfo ShaderStage{};
        ShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ShaderStage.stage = Stage;
        ShaderStage.module = ShaderModule;
        ShaderStage.pName = "main";

        return ShaderStage;
    }
};

#include "ShaderBuilder.hpp"

#include "Utils/IO.hpp"
#include "Renderer/VkVault.hpp"

VkPipelineShaderStageCreateInfo CreateShaderStage(
        VkShaderStageFlagBits stage,
        std::string filename,
        std::vector<char> &shader_code
    )
{
    u32 shader_size;
    IO::BinaryRead(filename, shader_code, shader_size);

    VkShaderModuleCreateInfo shader_module_create_info {};
    shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_module_create_info.codeSize = shader_size;
    shader_module_create_info.pCode = reinterpret_cast<const u32*>(shader_code.data());

    VkShaderModule shader_module {};
    if (vkCreateShaderModule(VkVault::Device, &shader_module_create_info, nullptr, &shader_module) != VK_SUCCESS) {
        analog::critical("shader creation failed and there's no save");
    }

    VkPipelineShaderStageCreateInfo shader_stage {};
    shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage.stage = stage;
    shader_stage.module = shader_module;
    shader_stage.pName = "main";

    return shader_stage;
}

#include "ShaderBuilder.hpp"

#include "Renderer/VkVault.hpp"
#include "Engine/Core/FileIO.hpp"

const char* ENTRY_POINT_NAME = "main";

IncResult CreateShaderStage(
    VkShaderStageFlagBits stage,
    std::string filename,
    std::vector<u32> &shader_code,
    VkPipelineShaderStageCreateInfo& shader_stage
) {
    u32 shader_size_in_bytes;
    INC_CHECK(
        FileIO::BinaryRead(filename, shader_code, shader_size_in_bytes),
        "shader from file: {} - couldn't be read", filename
    );

    VkShaderModuleCreateInfo shader_module_create_info {};
    shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_module_create_info.pNext = nullptr;
    shader_module_create_info.codeSize = shader_size_in_bytes;
    shader_module_create_info.pCode =shader_code.data();

    VkShaderModule shader_module {};
    VK_CHECK(
        vkCreateShaderModule(VkVault::Device, &shader_module_create_info, nullptr, &shader_module),
        "shader creation failed and there's no save"
    );

    shader_stage = {};
    shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage.stage = stage;
    shader_stage.module = shader_module;
    shader_stage.pName = ENTRY_POINT_NAME;

    return IncResult::SUCCESS;
}

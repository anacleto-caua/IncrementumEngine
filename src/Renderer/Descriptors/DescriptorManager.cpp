#include "DescriptorManager.hpp"

#include "Renderer/VkVault.hpp"

// TODO: I don't really like thoose wonky approximations
constexpr u32 MAX_SETS = RendererConfig::MAX_FRAMES_IN_FLIGHT * 50;

constexpr u32 MAX_UBOS = RendererConfig::MAX_FRAMES_IN_FLIGHT * 10;
constexpr u32 MAX_SAMPLERS = RendererConfig::MAX_FRAMES_IN_FLIGHT * 10;
constexpr u32 MAX_SSBOS = RendererConfig::MAX_FRAMES_IN_FLIGHT * 10;

constexpr VkShaderStageFlags ALL_SHADER_STAGES = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

namespace DescriptorManager {
    VkDescriptorPool Pool = VK_NULL_HANDLE;

    IncResult Create() {

        // =========================================================
        // 1. Create the Global Layout (Set 0)
        // =========================================================
        VkDescriptorSetLayoutBinding camera_ubo_binding = {
            .binding = DescriptorMap::Global::Binding_CameraUBO,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = ALL_SHADER_STAGES,
            .pImmutableSamplers = nullptr
        };

        VkDescriptorSetLayoutCreateInfo global_layout_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = 1,
            .pBindings = &camera_ubo_binding
        };

        VK_CHECK(
            vkCreateDescriptorSetLayout(VkVault::Device, &global_layout_info, nullptr, &GlobalLayout),
            "failed to create descriptor set layout"
        );

        // =========================================================
        // 2. Create the Terrain Layout (Set 1)
        // =========================================================
        VkDescriptorSetLayoutBinding heightmap_binding = {
            .binding = DescriptorMap::PerFrame::Binding_HeightmapTexture,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = ALL_SHADER_STAGES,
            .pImmutableSamplers = nullptr
        };

        VkDescriptorSetLayoutBinding chunk_ssbo_binding = {
            .binding = DescriptorMap::PerFrame::Binding_ChunkDrawListSSBO,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = ALL_SHADER_STAGES,
            .pImmutableSamplers = nullptr
        };

        std::array<VkDescriptorSetLayoutBinding, 2> terrain_bindings = { heightmap_binding, chunk_ssbo_binding };

        VkDescriptorSetLayoutCreateInfo terrain_layout_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = static_cast<uint32_t>(terrain_bindings.size()),
            .pBindings = terrain_bindings.data()
        };

        VK_CHECK(
            vkCreateDescriptorSetLayout(VkVault::Device, &terrain_layout_info, nullptr, &PerFrameLayout),
            "failed to create descriptor set layout"
        );

        // Create the whole engine descriptor pool
        std::array<VkDescriptorPoolSize, 3> pool_sizes = {{
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,            MAX_UBOS},
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,    MAX_SAMPLERS },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,            MAX_SSBOS}
        }};

        // maxSets needs to be large enough to hold every allocated set in the engine
        VkDescriptorPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, // Allows freeing individual sets if needed later (not used)
            .maxSets = MAX_SETS,
            .poolSizeCount = static_cast<u32>(pool_sizes.size()),
            .pPoolSizes = pool_sizes.data()
        };

        VK_CHECK(
            vkCreateDescriptorPool(VkVault::Device, &pool_info, nullptr, &Pool),
            "failed to create descriptor pool"
        );

        return IncResult::SUCCESS;
    }

    void Destroy() {
        if (Pool) { vkDestroyDescriptorPool(VkVault::Device, Pool, nullptr); }
        if (GlobalLayout) { vkDestroyDescriptorSetLayout(VkVault::Device, GlobalLayout, nullptr); }
        if (PerFrameLayout) { vkDestroyDescriptorSetLayout(VkVault::Device, PerFrameLayout, nullptr); }
    }

    VkDescriptorSet AllocateSet(VkDescriptorSetLayout layout) {
        VkDescriptorSet set;
        AllocateSets(layout, 1, &set);
        return set;
    }

    void AllocateSets(VkDescriptorSetLayout layout, u32 count, VkDescriptorSet* outSets) {
        std::vector<VkDescriptorSetLayout> layouts(count, layout);
        VkDescriptorSetAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = Pool,
            .descriptorSetCount = count,
            .pSetLayouts = layouts.data()
        };

        vkAllocateDescriptorSets(VkVault::Device, &alloc_info, outSets);
    }
}

#include "DescriptorManager.hpp"

#include "Renderer/VkVault.hpp"
#include "Renderer/Renderer.hpp"

// TODO: I don't really like thoose wonky approximations
constexpr u32 MAX_SETS = Renderer::MAX_FRAMES_IN_FLIGHT * 50;

constexpr u32 MAX_UBOS = Renderer::MAX_FRAMES_IN_FLIGHT * 10;
constexpr u32 MAX_SAMPLERS = Renderer::MAX_FRAMES_IN_FLIGHT * 10;
constexpr u32 MAX_SSBOS = Renderer::MAX_FRAMES_IN_FLIGHT * 10;
constexpr u32 MAX_STORAGE_IMAGES = Renderer::MAX_FRAMES_IN_FLIGHT * 4;

// Includes VK_SHADER_STAGE_COMPUTE_BIT so a compute pipeline can bind Set 0 (SceneGlobals) too -
// not exercised by ComputeTestLayout below (that's its own standalone layout, never combined with
// GlobalLayout), but closes the gap for a future compute job that wants scene globals.
constexpr VkShaderStageFlags ALL_SHADER_STAGES = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

namespace DescriptorManager {
    VkDescriptorPool Pool = VK_NULL_HANDLE;

    IncResult Create() {

        // =========================================================
        // 1. Create the Global Layout (Set 0)
        // =========================================================
        VkDescriptorSetLayoutBinding camera_ubo_binding = {
            .binding = DescriptorMap::Global::Binding_SceneGlobals,
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

        // =========================================================
        // 3. Create the Prop Layout (also Set 1, a separate layout object - see the comment on
        //    DescriptorMap::PropPerFrame)
        // =========================================================
        VkDescriptorSetLayoutBinding prop_instance_ssbo_binding = {
            .binding = DescriptorMap::PropPerFrame::Binding_InstanceSSBO,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = ALL_SHADER_STAGES,
            .pImmutableSamplers = nullptr
        };

        VkDescriptorSetLayoutBinding prop_texture_binding = {
            .binding = DescriptorMap::PropPerFrame::Binding_Texture,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, // only prop.frag samples it
            .pImmutableSamplers = nullptr
        };

        std::array<VkDescriptorSetLayoutBinding, 2> prop_bindings = { prop_instance_ssbo_binding, prop_texture_binding };

        VkDescriptorSetLayoutCreateInfo prop_layout_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = static_cast<u32>(prop_bindings.size()),
            .pBindings = prop_bindings.data()
        };

        VK_CHECK(
            vkCreateDescriptorSetLayout(VkVault::Device, &prop_layout_info, nullptr, &PropPerFrameLayout),
            "failed to create descriptor set layout"
        );

        // =========================================================
        // 4. Create the Compute Test Layout (its own standalone Set 0)
        // =========================================================
        VkDescriptorSetLayoutBinding compute_test_buffer_binding = {
            .binding = DescriptorMap::ComputeTest::Binding_TestBuffer,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, // only ever bound in a compute pipeline
            .pImmutableSamplers = nullptr
        };

        VkDescriptorSetLayoutCreateInfo compute_test_layout_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = 1,
            .pBindings = &compute_test_buffer_binding
        };

        VK_CHECK(
            vkCreateDescriptorSetLayout(VkVault::Device, &compute_test_layout_info, nullptr, &ComputeTestLayout),
            "failed to create descriptor set layout"
        );

        // Create the whole engine descriptor pool
        std::array<VkDescriptorPoolSize, 4> pool_sizes = {{
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,            MAX_UBOS},
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,    MAX_SAMPLERS },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,            MAX_SSBOS},
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,             MAX_STORAGE_IMAGES}
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
        if (PropPerFrameLayout) { vkDestroyDescriptorSetLayout(VkVault::Device, PropPerFrameLayout, nullptr); }
        if (ComputeTestLayout) { vkDestroyDescriptorSetLayout(VkVault::Device, ComputeTestLayout, nullptr); }
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

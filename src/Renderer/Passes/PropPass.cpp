#include "PropPass.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <bit>

#include <stb/stb_image.h>

#include <imgui.h>

#include "Game/Game.hpp"
#include "Renderer/VkVault.hpp"
#include "Renderer/Vk/ShaderBuilder.hpp"
#include "Renderer/Vk/PipelineDefaults.hpp"
#include "Renderer/Vk/ShaderSpecializationBuilder.hpp"
#include "Renderer/Vk/LeanVk.hpp"
#include "Renderer/Vk/SubmissionPile.hpp"
#include "Renderer/Vk/CommandBufferBlock.hpp"
#include "Renderer/Vk/TimelineSemaphore.hpp"
#include "Renderer/Tools/DebugPanel.hpp"
#include "Renderer/Resources/ImageView.hpp"
#include "Renderer/Tools/ModelLoader.hpp"
#include "Renderer/Camera.hpp"
#include "Renderer/Descriptors/DescriptorManager.hpp"

namespace {
    // RGBA8 image in CPU memory, row-major, top row first.
    struct Rgba8Image {
        u32 Width = 0;
        u32 Height = 0;
        std::vector<u8> Pixels;
    };

    f32 SrgbToLinear(f32 c) {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    // Bilinear resample to size x size. Exact 2x2 box average for a 2:1 downscale, which is the
    // common case (2048 source -> 1024 layer).
    Rgba8Image Resample(const Rgba8Image& src, u32 size) {
        Rgba8Image dst { size, size, std::vector<u8>(static_cast<size_t>(size) * size * 4) };
        f32 sx = static_cast<f32>(src.Width) / static_cast<f32>(size);
        f32 sy = static_cast<f32>(src.Height) / static_cast<f32>(size);

        auto at = [&](u32 px, u32 py, u32 c) {
            return static_cast<f32>(src.Pixels[(static_cast<size_t>(py) * src.Width + px) * 4 + c]);
        };

        for (u32 y = 0; y < size; y++) {
            f32 fy = std::clamp((static_cast<f32>(y) + 0.5f) * sy - 0.5f, 0.0f, static_cast<f32>(src.Height - 1));
            u32 y0 = static_cast<u32>(fy);
            u32 y1 = std::min(y0 + 1, src.Height - 1);
            f32 ty = fy - static_cast<f32>(y0);

            for (u32 x = 0; x < size; x++) {
                f32 fx = std::clamp((static_cast<f32>(x) + 0.5f) * sx - 0.5f, 0.0f, static_cast<f32>(src.Width - 1));
                u32 x0 = static_cast<u32>(fx);
                u32 x1 = std::min(x0 + 1, src.Width - 1);
                f32 tx = fx - static_cast<f32>(x0);

                for (u32 c = 0; c < 4; c++) {
                    f32 top = at(x0, y0, c) + (at(x1, y0, c) - at(x0, y0, c)) * tx;
                    f32 bottom = at(x0, y1, c) + (at(x1, y1, c) - at(x0, y1, c)) * tx;
                    f32 value = top + (bottom - top) * ty;
                    dst.Pixels[(static_cast<size_t>(y) * size + x) * 4 + c] = static_cast<u8>(std::clamp(value + 0.5f, 0.0f, 255.0f));
                }
            }
        }
        return dst;
    }

    // Edge padding: transparent texels inherit the average color of their opaque neighbors, a few
    // texels deep. Without it, bilinear filtering and mipmapping blend the (usually black or white)
    // color hidden under alpha = 0 into every leaf edge, leaving a dark/light outline.
    void DilateTransparentColor(Rgba8Image& image, u32 iterations) {
        constexpr u8 OpaqueThreshold = 128;
        u32 w = image.Width;
        u32 h = image.Height;
        std::vector<u8> filled(static_cast<size_t>(w) * h);
        for (size_t i = 0; i < filled.size(); i++) { filled[i] = image.Pixels[i * 4 + 3] >= OpaqueThreshold ? 1 : 0; }

        std::vector<u8> next_filled = filled;
        for (u32 iteration = 0; iteration < iterations; iteration++) {
            bool changed = false;
            for (u32 y = 0; y < h; y++) {
                for (u32 x = 0; x < w; x++) {
                    size_t i = static_cast<size_t>(y) * w + x;
                    if (filled[i]) { continue; }

                    u32 sum[3] = { 0, 0, 0 };
                    u32 count = 0;
                    for (i32 dy = -1; dy <= 1; dy++) {
                        for (i32 dx = -1; dx <= 1; dx++) {
                            i64 nx = static_cast<i64>(x) + dx;
                            i64 ny = static_cast<i64>(y) + dy;
                            if (nx < 0 || ny < 0 || nx >= static_cast<i64>(w) || ny >= static_cast<i64>(h)) { continue; }
                            size_t n = static_cast<size_t>(ny) * w + static_cast<size_t>(nx);
                            if (!filled[n]) { continue; }
                            for (u32 c = 0; c < 3; c++) { sum[c] += image.Pixels[n * 4 + c]; }
                            count++;
                        }
                    }
                    if (count == 0) { continue; }
                    for (u32 c = 0; c < 3; c++) { image.Pixels[i * 4 + c] = static_cast<u8>(sum[c] / count); }
                    next_filled[i] = 1;
                    changed = true;
                }
            }
            filled = next_filled;
            if (!changed) { break; }
        }
    }

    // Alpha-weighted 2x2 box downsample (color averaged by alpha, so transparent texels don't
    // darken the result).
    Rgba8Image Downsample(const Rgba8Image& src) {
        u32 w = std::max(src.Width / 2, 1u);
        u32 h = std::max(src.Height / 2, 1u);
        Rgba8Image dst { w, h, std::vector<u8>(static_cast<size_t>(w) * h * 4) };

        for (u32 y = 0; y < h; y++) {
            for (u32 x = 0; x < w; x++) {
                f32 color[3] = { 0.0f, 0.0f, 0.0f };
                f32 plain[3] = { 0.0f, 0.0f, 0.0f };
                f32 alpha = 0.0f;
                for (u32 sy = 0; sy < 2; sy++) {
                    for (u32 sx = 0; sx < 2; sx++) {
                        u32 px = std::min(x * 2 + sx, src.Width - 1);
                        u32 py = std::min(y * 2 + sy, src.Height - 1);
                        const u8* texel = &src.Pixels[(static_cast<size_t>(py) * src.Width + px) * 4];
                        f32 a = static_cast<f32>(texel[3]) / 255.0f;
                        for (u32 c = 0; c < 3; c++) {
                            color[c] += static_cast<f32>(texel[c]) * a;
                            plain[c] += static_cast<f32>(texel[c]);
                        }
                        alpha += a;
                    }
                }
                u8* out = &dst.Pixels[(static_cast<size_t>(y) * w + x) * 4];
                for (u32 c = 0; c < 3; c++) {
                    f32 value = alpha > 0.0f ? color[c] / alpha : plain[c] * 0.25f;
                    out[c] = static_cast<u8>(std::clamp(value + 0.5f, 0.0f, 255.0f));
                }
                out[3] = static_cast<u8>(std::clamp(alpha * 0.25f * 255.0f + 0.5f, 0.0f, 255.0f));
            }
        }
        return dst;
    }

    f32 AlphaCoverage(const Rgba8Image& image, f32 scale) {
        constexpr f32 Cutoff = 0.5f * 255.0f;
        size_t covered = 0;
        size_t count = static_cast<size_t>(image.Width) * image.Height;
        for (size_t i = 0; i < count; i++) {
            if (static_cast<f32>(image.Pixels[i * 4 + 3]) * scale > Cutoff) { covered++; }
        }
        return static_cast<f32>(covered) / static_cast<f32>(count);
    }

    // Scales a mip's alpha so the fraction of texels above the cutoff matches mip 0's (Castano's
    // coverage-preserving mipmaps). Plain box-filtered alpha drifts toward 0.5 and below, so alpha-
    // tested foliage visibly thins out and goes bald with distance without this.
    void PreserveAlphaCoverage(Rgba8Image& mip, f32 target_coverage) {
        f32 low = 0.0f;
        f32 high = 4.0f;
        for (u32 step = 0; step < 12; step++) {
            f32 mid = (low + high) * 0.5f;
            if (AlphaCoverage(mip, mid) < target_coverage) { low = mid; } else { high = mid; }
        }
        f32 scale = (low + high) * 0.5f;
        size_t count = static_cast<size_t>(mip.Width) * mip.Height;
        for (size_t i = 0; i < count; i++) {
            f32 a = static_cast<f32>(mip.Pixels[i * 4 + 3]) * scale;
            mip.Pixels[i * 4 + 3] = static_cast<u8>(std::clamp(a + 0.5f, 0.0f, 255.0f));
        }
    }

    bool LoadRgba8(const std::string& path, Rgba8Image& out) {
        int w = 0, h = 0, channels = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (!pixels) {
            analog::warn("PropPass: couldn't load texture {} ({}) - using plain white", path, stbi_failure_reason());
            return false;
        }
        out.Width = static_cast<u32>(w);
        out.Height = static_cast<u32>(h);
        out.Pixels.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
        stbi_image_free(pixels);
        return true;
    }

    // Builds one layer's full mip chain (mip 0 first) from a texture source. Falls back to plain
    // white when the diffuse image can't be read, so a broken path degrades instead of failing Init.
    std::vector<Rgba8Image> BuildLayerMips(const std::string& diffuse, const std::string& alpha, u32 size, bool& out_has_cutout) {
        Rgba8Image base;
        if (diffuse.empty() || !LoadRgba8(diffuse, base)) {
            base = { 1, 1, { 255, 255, 255, 255 } };
        }
        base = Resample(base, size);

        if (!alpha.empty()) {
            Rgba8Image mask;
            if (LoadRgba8(alpha, mask)) {
                mask = Resample(mask, size);
                // Grayscale masks carry the mask in RGB; masks with a real alpha channel in A.
                bool mask_has_alpha = false;
                for (size_t i = 0; i < static_cast<size_t>(size) * size && !mask_has_alpha; i++) { mask_has_alpha = mask.Pixels[i * 4 + 3] < 255; }
                for (size_t i = 0; i < static_cast<size_t>(size) * size; i++) {
                    base.Pixels[i * 4 + 3] = mask_has_alpha ? mask.Pixels[i * 4 + 3] : mask.Pixels[i * 4 + 0];
                }
            }
        }

        bool has_cutout = false;
        for (size_t i = 0; i < static_cast<size_t>(size) * size && !has_cutout; i++) { has_cutout = base.Pixels[i * 4 + 3] < 255; }
        out_has_cutout = has_cutout;
        if (has_cutout) { DilateTransparentColor(base, 16); }
        f32 coverage = has_cutout ? AlphaCoverage(base, 1.0f) : 1.0f;

        std::vector<Rgba8Image> mips;
        mips.push_back(std::move(base));
        while (mips.back().Width > 1 || mips.back().Height > 1) {
            Rgba8Image next = Downsample(mips.back());
            if (has_cutout) { PreserveAlphaCoverage(next, coverage); }
            mips.push_back(std::move(next));
        }
        return mips;
    }

    u8 MipCount(u32 size) {
        u8 count = 1;
        while (size > 1) { size /= 2; count++; }
        return count;
    }
}

u32 PropPass::TextureLayerFor(const std::string& diffuse, const std::string& alpha) {
    if (diffuse.empty()) { return 0; }
    for (u32 i = 0; i < TextureSources.size(); i++) {
        if (TextureSources[i].Diffuse == diffuse && TextureSources[i].Alpha == alpha) { return i + 1; }
    }
    if (TextureSources.size() + 1 >= MaxTextureLayers) {
        analog::warn("PropPass: more than {} prop textures - {} drawn untextured", MaxTextureLayers - 1, diffuse);
        return 0;
    }
    TextureSources.push_back({ diffuse, alpha });
    return static_cast<u32>(TextureSources.size());
}

// Layer 0 is plain white, every other layer is one TextureSources entry. Each layer gets its full
// mip chain built on the CPU, then everything goes up in one startup-only Graphics-queue submit
// (local CommandBufferBlock + SubmissionPile, blocking wait - same posture as the rest of Init()).
IncResult PropPass::CreateTextures() {
    u32 layer_count = static_cast<u32>(TextureSources.size()) + 1;
    u8 mip_count = MipCount(TextureSize);

    std::vector<std::vector<Rgba8Image>> layers;
    layers.reserve(layer_count);
    std::vector<u8> layer_has_cutout(layer_count, 0);
    bool white_has_cutout = false;
    layers.push_back(BuildLayerMips({}, {}, TextureSize, white_has_cutout));
    for (const TextureSource& source : TextureSources) {
        bool has_cutout = false;
        layers.push_back(BuildLayerMips(source.Diffuse, source.Alpha, TextureSize, has_cutout));
        layer_has_cutout[layers.size() - 1] = has_cutout ? 1 : 0;
        analog::info("PropPass: texture layer {} <- {}{}", layers.size() - 1, source.Diffuse, has_cutout ? " (cut-out foliage)" : "");
    }
    for (ModelResource& model : Models) {
        for (SubMeshResource& sub_mesh : model.SubMeshes) { sub_mesh.Foliage = layer_has_cutout[sub_mesh.TextureLayer] != 0; }
    }

    Image::CreateInfo texture_image_info;
    texture_image_info.Width = TextureSize;
    texture_image_info.Height = TextureSize;
    texture_image_info.MipLevels = mip_count;
    texture_image_info.ArrayLayers = layer_count;
    texture_image_info.Format = VK_FORMAT_R8G8B8A8_SRGB;  // albedo is authored in sRGB; sampling returns linear
    texture_image_info.Usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    texture_image_info.OwnerQueue = QueueRole::Graphics;
    texture_image_info.UsageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    INC_CHECK(Images.Add(texture_image_info, TextureImage), "prop texture image creation failed");
    Image* texture_image_value = Images.Get(TextureImage);
    auto texture_image_view_info = FillImageViewCreateInfo(texture_image_value);
    INC_CHECK(ImageViews.Add(texture_image_view_info, TextureView), "prop texture image view creation failed");

    // Staging: every (layer, mip) back to back, one copy region each.
    size_t staging_size = 0;
    for (const auto& mips : layers) {
        for (const Rgba8Image& mip : mips) { staging_size += mip.Pixels.size(); }
    }

    BufferId staging_buffer;
    INC_CHECK(Buffers.Add({ .Size = staging_size, .Type = Buffer::Type::STAGING }, staging_buffer), "prop texture staging buffer creation failed");

    std::vector<VkBufferImageCopy> copy_regions;
    copy_regions.reserve(static_cast<size_t>(layer_count) * mip_count);
    {
        u8* mapped = static_cast<u8*>(Buffers.Map(staging_buffer));
        size_t offset = 0;
        for (u32 layer = 0; layer < layer_count; layer++) {
            for (u32 mip = 0; mip < layers[layer].size(); mip++) {
                const Rgba8Image& image = layers[layer][mip];
                std::memcpy(mapped + offset, image.Pixels.data(), image.Pixels.size());

                VkBufferImageCopy region {};
                region.bufferOffset = offset;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = mip;
                region.imageSubresource.baseArrayLayer = layer;
                region.imageSubresource.layerCount = 1;
                region.imageExtent = { image.Width, image.Height, 1 };
                copy_regions.push_back(region);

                offset += image.Pixels.size();
            }
        }
        Buffers.Unmap(staging_buffer);
    }

    CommandBufferBlock command_block;
    INC_CHECK(command_block.Init(QueueRole::Graphics), "prop texture command buffer block creation failed");
    TimelineSemaphore upload_semaphore;
    INC_CHECK(upload_semaphore.Init(), "prop texture upload semaphore creation failed");

    VkCommandBuffer cmd = command_block.GetNext();
    LeanVk::BeginCommand(cmd);

    VkImageMemoryBarrier2 barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.srcAccessMask = VK_ACCESS_2_NONE;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture_image_value->Handle;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_count, 0, layer_count };

    VkDependencyInfo dependency {};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);

    vkCmdCopyBufferToImage(
        cmd, Buffers.Get(staging_buffer)->Handle, texture_image_value->Handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<u32>(copy_regions.size()), copy_regions.data()
    );

    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = texture_image_value->UsageLayout;
    vkCmdPipelineBarrier2(cmd, &dependency);

    LeanVk::EndCommand(cmd);

    SubmissionPile<QueueRole::Graphics, 1, 1, 1, 1> pile;
    pile.BeginSubmission();
    pile.AddCommand(cmd);
    pile.SignalTimeline(upload_semaphore, 1);
    pile.EndSubmission();
    pile.Submit();
    upload_semaphore.Wait(1);

    upload_semaphore.Destroy();
    command_block.Destroy();
    Buffers.Del(staging_buffer);

    VkSamplerCreateInfo texture_sampler_info {};
    texture_sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    texture_sampler_info.magFilter = VK_FILTER_LINEAR;
    texture_sampler_info.minFilter = VK_FILTER_LINEAR;
    texture_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    texture_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    texture_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    texture_sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    texture_sampler_info.anisotropyEnable = VK_TRUE;  // samplerAnisotropy is enabled in VkVault
    texture_sampler_info.maxAnisotropy = 8.0f;
    texture_sampler_info.unnormalizedCoordinates = VK_FALSE;
    texture_sampler_info.compareEnable = VK_FALSE;
    texture_sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    texture_sampler_info.mipLodBias = 0.0f;
    texture_sampler_info.minLod = 0.0f;
    texture_sampler_info.maxLod = VK_LOD_CLAMP_NONE;

    VK_CHECK(
        vkCreateSampler(VkVault::Device, &texture_sampler_info, nullptr, &TextureSampler),
        "prop texture sampler creation failed"
    );

    return IncResult::SUCCESS;
}

IncResult PropPass::LoadModel(TerrainManager::PropModel model, const char* path, vec3 base_color, f32 target_height) {
    ModelLoader::Model cpu_model;
    INC_CHECK(ModelLoader::LoadObj(path, cpu_model), "failed to load prop model {}", path);
    ModelLoader::NormalizeHeight(cpu_model, target_height, 0.03f);

    u32 index = static_cast<u32>(model);
    ModelResource& resource = Models[index];

    glm::vec3 fallback_tint = { SrgbToLinear(base_color.x), SrgbToLinear(base_color.y), SrgbToLinear(base_color.z) };
    for (const ModelLoader::SubMesh& sub_mesh : cpu_model.SubMeshes) {
        SubMeshResource sub_mesh_resource {};
        sub_mesh_resource.FirstIndex = sub_mesh.FirstIndex;
        sub_mesh_resource.IndexCount = sub_mesh.IndexCount;
        sub_mesh_resource.TextureLayer = TextureLayerFor(sub_mesh.DiffuseTexture, sub_mesh.AlphaTexture);
        // Kd multiplies the texture too, so a sourced texture can be tuned to sit with the terrain
        // by editing the .mtl alone. Exporters often write a 0.64/0.8 grey default here - set it
        // to 1 for a texture that should show as authored.
        sub_mesh_resource.Tint = sub_mesh.HasMaterial ? glm::vec3(sub_mesh.DiffuseColor) : fallback_tint;
        resource.SubMeshes.push_back(sub_mesh_resource);
    }
    analog::info("PropPass: {} - {} vertices, {} triangles, {} submeshes", path,
        cpu_model.Vertices.size(), cpu_model.Indices.size() / 3, cpu_model.SubMeshes.size());

    Buffer::CreateInfo vertex_buffer_info = {
        .Size = cpu_model.Vertices.size() * sizeof(ModelLoader::Vertex),
        .Type = Buffer::Type::VERTEX,
    };
    INC_CHECK(Buffers.Add(vertex_buffer_info, resource.VertexBuffer), "prop vertex buffer creation failed for {}", path);

    Buffer::CreateInfo index_buffer_info = {
        .Size = cpu_model.Indices.size() * sizeof(u32),
        .Type = Buffer::Type::INDEX,
    };
    INC_CHECK(Buffers.Add(index_buffer_info, resource.IndexBuffer), "prop index buffer creation failed for {}", path);

    GTransferPipe.QueueBufferUpload(resource.VertexBuffer, 0, cpu_model.Vertices.data(), vertex_buffer_info.Size);
    GTransferPipe.QueueBufferUpload(resource.IndexBuffer, 0, cpu_model.Indices.data(), index_buffer_info.Size);

    return IncResult::SUCCESS;
}

IncResult PropPass::Init() {
    // Model table. Drop a sourced .obj (+ its .mtl and textures, referenced relative to the .obj)
    // at these paths to swap the art with no code change: each material becomes its own submesh,
    // and the mesh is rescaled to the target height (metres) regardless of the units it was
    // authored in. The base color only applies to faces with no .mtl material at all (the
    // hand-made placeholders). Buffer uploads are flushed by the one LazySubmit() below -
    // startup-only, so the blocking wait is fine (same posture as TerrainPass::PlaneMeshResource).
    INC_CHECK(LoadModel(TerrainManager::PropModel::Tree, "assets/models/tree.obj", vec3(0.30f, 0.45f, 0.15f), 9.0f), "tree model load failed");
    INC_CHECK(LoadModel(TerrainManager::PropModel::Rock, "assets/models/rock.obj", vec3(0.45f, 0.43f, 0.40f), 1.6f), "rock model load failed");
    INC_CHECK(CreateTextures(), "prop texture creation failed");
    GTransferPipe.LazySubmit();

    // Per-model, per-frame-in-flight instance SSBO + descriptor set
    {
        Buffer::CreateInfo instance_buffer_info = {
            .Size = MaxInstancesPerModel * sizeof(GpuInstance),
            .Type = Buffer::Type::SSBO,
        };

        for (u32 model = 0; model < ModelCount; model++) {
            for (u32 frame = 0; frame < RendererConstants::MAX_FRAMES_IN_FLIGHT; frame++) {
                INC_CHECK(Buffers.Add(instance_buffer_info, InstanceBuffers[model][frame]), "prop instance buffer creation failed");
            }

            DescriptorManager::AllocateSets(
                DescriptorManager::PropPerFrameLayout,
                RendererConstants::MAX_FRAMES_IN_FLIGHT,
                DescriptorSets[model].data()
            );

            auto texture_view_value = ImageViews.Get(TextureView);
            VkDescriptorImageInfo texture_descriptor {};
            texture_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            texture_descriptor.imageView = texture_view_value->Handle;
            texture_descriptor.sampler = TextureSampler;

            for (u32 frame = 0; frame < RendererConstants::MAX_FRAMES_IN_FLIGHT; frame++) {
                auto instance_buffer_value = Buffers.Get(InstanceBuffers[model][frame]);
                VkDescriptorBufferInfo instance_buffer_descriptor {};
                instance_buffer_descriptor.buffer = instance_buffer_value->Handle;
                instance_buffer_descriptor.offset = 0;
                instance_buffer_descriptor.range = instance_buffer_value->Size;

                std::array<VkWriteDescriptorSet, 2> writes {};
                writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[0].dstSet = DescriptorSets[model][frame];
                writes[0].dstBinding = DescriptorMap::PropPerFrame::Binding_InstanceSSBO;
                writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[0].descriptorCount = 1;
                writes[0].pBufferInfo = &instance_buffer_descriptor;

                writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[1].dstSet = DescriptorSets[model][frame];
                writes[1].dstBinding = DescriptorMap::PropPerFrame::Binding_Texture;
                writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[1].descriptorCount = 1;
                writes[1].pImageInfo = &texture_descriptor;

                vkUpdateDescriptorSets(VkVault::Device, static_cast<u32>(writes.size()), writes.data(), 0, nullptr);
            }

            StagingInstances[model].reserve(MaxInstancesPerModel);
        }
    }

    // Pipeline layout
    std::array<VkDescriptorSetLayout, 2> pipeline_layouts = {
        DescriptorManager::GlobalLayout,
        DescriptorManager::PropPerFrameLayout
    };

    VkPipelineLayoutCreateInfo prop_pipeline_layout_create_info {};
    prop_pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    prop_pipeline_layout_create_info.setLayoutCount = static_cast<u32>(pipeline_layouts.size());
    prop_pipeline_layout_create_info.pSetLayouts = pipeline_layouts.data();
    VkPushConstantRange push_constant_range {};
    push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PushConstants);
    prop_pipeline_layout_create_info.pushConstantRangeCount = 1;
    prop_pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;

    VK_CHECK(
        vkCreatePipelineLayout(VkVault::Device, &prop_pipeline_layout_create_info, nullptr, &PropPipelineLayout),
        "prop pipeline layout creation failed"
    );

    // Real (non-empty) vertex input state - the first pipeline in the codebase to need one, since
    // terrain generates its vertices procedurally in-shader (PipelineDefaults's default vertex
    // input state deliberately declares zero bindings/attributes for that reason).
    VkVertexInputBindingDescription vertex_binding {};
    vertex_binding.binding = 0;
    vertex_binding.stride = sizeof(ModelLoader::Vertex);
    vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> vertex_attributes {{
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(ModelLoader::Vertex, Position) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(ModelLoader::Vertex, Normal) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(ModelLoader::Vertex, UV) }
    }};

    VkPipelineVertexInputStateCreateInfo vertex_input_state {};
    vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount = 1;
    vertex_input_state.pVertexBindingDescriptions = &vertex_binding;
    vertex_input_state.vertexAttributeDescriptionCount = static_cast<u32>(vertex_attributes.size());
    vertex_input_state.pVertexAttributeDescriptions = vertex_attributes.data();

    auto dynamic_state_create_info = PipelineDefaults::DefaultPipelineDynamicStateCreateInfo();

    auto rendering_create_info = PipelineDefaults::SceneRenderingCreateInfo();

    auto input_assembly_state = PipelineDefaults::DefaultPipelineInputAssemblyStateCreateInfo();
    auto viewport_state = PipelineDefaults::DefaultPipelineViewportStateCreateInfo();
    auto rasterization_state = PipelineDefaults::DefaultPipelineRasterizationStateCreateInfo();
    auto multisample_state = PipelineDefaults::SceneMultisampleStateCreateInfo();
    // Cut-out foliage: prop.frag outputs a sharpened alpha that becomes per-sample coverage on the
    // 4x MSAA scene target - antialiased leaf edges, no sorting, unlike blending. Opaque texels
    // output alpha 1 (full coverage), so untextured/opaque props are unaffected.
    multisample_state.alphaToCoverageEnable = VK_TRUE;
    auto depth_stencil_state = PipelineDefaults::DefaultPipelineDepthStencilStateCreateInfo();
    auto colorblend_state = PipelineDefaults::DefaultPipelineColorBlendStateCreateInfo();

    VkGraphicsPipelineCreateInfo prop_pipeline_create_info {};
    prop_pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    prop_pipeline_create_info.pVertexInputState = &vertex_input_state;
    prop_pipeline_create_info.pInputAssemblyState = &input_assembly_state;
    prop_pipeline_create_info.pViewportState = &viewport_state;
    prop_pipeline_create_info.pRasterizationState = &rasterization_state;
    prop_pipeline_create_info.pMultisampleState = &multisample_state;
    prop_pipeline_create_info.pDepthStencilState = &depth_stencil_state;
    prop_pipeline_create_info.pColorBlendState = &colorblend_state;
    prop_pipeline_create_info.pDynamicState = &dynamic_state_create_info;
    prop_pipeline_create_info.layout = PropPipelineLayout;
    prop_pipeline_create_info.basePipelineIndex = -1;
    prop_pipeline_create_info.pNext = &rendering_create_info;

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    std::vector<u32> shader_buffer;
    shader_buffer.reserve(4096);

    VkPipelineShaderStageCreateInfo vert_shader;
    INC_CHECK(CreateShaderStage(VK_SHADER_STAGE_VERTEX_BIT, "shaders/prop.vert.spv", shader_buffer, vert_shader), "prop vertex shader creation failed");

    VkPipelineShaderStageCreateInfo frag_shader;
    INC_CHECK(CreateShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/prop.frag.spv", shader_buffer, frag_shader), "prop fragment shader creation failed");
    // FOG_END - same value TerrainPass feeds terrain.frag, so props fog at the ground's own rate.
    SpecializationBuilder frag_shader_spec_builder;
    frag_shader_spec_builder.AddConstant(10, static_cast<f32>(TerrainManager::TotalCoverageRadius));
    frag_shader.pSpecializationInfo = frag_shader_spec_builder.Build();

    shader_stages.push_back(vert_shader);
    shader_stages.push_back(frag_shader);

    prop_pipeline_create_info.stageCount = static_cast<u32>(shader_stages.size());
    prop_pipeline_create_info.pStages = shader_stages.data();

    VK_CHECK(
        vkCreateGraphicsPipelines(VkVault::Device, VK_NULL_HANDLE, 1, &prop_pipeline_create_info, nullptr, &PropPipeline),
        "prop pipeline creation failed"
    );

    for (auto shader_stage : shader_stages) {
        if (shader_stage.module) { vkDestroyShaderModule(VkVault::Device, shader_stage.module, nullptr); }
    }

    return IncResult::SUCCESS;
}

void PropPass::Destroy() {
    for (u32 model = 0; model < ModelCount; model++) {
        for (auto& buffer : InstanceBuffers[model]) { Buffers.Del(buffer); }
        Buffers.Del(Models[model].VertexBuffer);
        Buffers.Del(Models[model].IndexBuffer);
    }

    if (PropPipeline) { vkDestroyPipeline(VkVault::Device, PropPipeline, nullptr); }
    if (PropPipelineLayout) { vkDestroyPipelineLayout(VkVault::Device, PropPipelineLayout, nullptr); }

    if (TextureSampler) { vkDestroySampler(VkVault::Device, TextureSampler, nullptr); }
    ImageViews.Del(TextureView);
    Images.Del(TextureImage);
}

void PropPass::FrameSensibleTransfers() {
    using namespace TerrainManager;

    for (u32 model = 0; model < ModelCount; model++) { StagingInstances[model].clear(); }
    if (Mode == DrawMode::Off) { return; }

    glm::vec2 camera_xz = GRenderer.CurrentCamera
        ? glm::vec2(GRenderer.CurrentCamera->Position.x, GRenderer.CurrentCamera->Position.z)
        : glm::vec2(0.0f);
    f32 draw_distance_sq = DrawDistance * DrawDistance;

    for (u32 i = 0; i < CurrentlyActiveChunks; i++) {
        const ChunkInstanceData& chunk = ChunkDrawList[i];

        // Whole-chunk reject first: center distance minus the chunk's half-diagonal.
        glm::vec2 chunk_center = (glm::vec2(chunk.WorldPos) + 0.5f) * chunk.Scale;
        f32 chunk_reach = glm::length(chunk_center - camera_xz) - chunk.Scale * 0.70711f;
        if (chunk_reach > DrawDistance) { continue; }

        const PropPlacement& props = GetPropsForLayer(chunk.TextureLayer);

        for (u32 p = 0; p < props.Count; p++) {
            const PropInstance& instance = props.Instances[p];
            glm::vec2 offset = glm::vec2(instance.WorldPosition.x, instance.WorldPosition.z) - camera_xz;
            if (glm::dot(offset, offset) > draw_distance_sq) { continue; }

            u32 model_index = static_cast<u32>(instance.Model);

            // Per-instance color variation from a position hash - stable across frames and
            // re-streams, so no extra placement data. Brightness 0.85..1.1 plus a slight warm/cool
            // shift: enough to break up identical copies of one mesh.
            u32 hash = (std::bit_cast<u32>(instance.WorldPosition.x) * 0x9E3779B1u) ^ (std::bit_cast<u32>(instance.WorldPosition.z) * 0x85EBCA77u);
            hash ^= hash >> 15;
            hash *= 0x2C1B3C6Du;
            hash ^= hash >> 12;
            f32 brightness = 0.85f + 0.25f * static_cast<f32>(hash & 0xFFu) / 255.0f;
            f32 warmth = (static_cast<f32>((hash >> 8) & 0xFFu) / 255.0f - 0.5f) * 0.12f;
            glm::vec3 tint = brightness * glm::vec3(1.0f + warmth, 1.0f, 1.0f - warmth);

            GpuInstance gpu_instance {};
            gpu_instance.PositionAndRotation = vec4(instance.WorldPosition, instance.YRotation);
            gpu_instance.ScaleAndColor = vec4(instance.Scale, tint);

            StagingInstances[model_index].push_back(gpu_instance);
        }
    }

    u32 frame_index = GRenderer.FrameContext.FrameInFlightIndex;

    // vkCmdUpdateBuffer caps a single call at 65536 bytes (a hard Vulkan limit) and
    // MaxInstancesPerModel's worst case exceeds that, so this uploads in <=65536-byte chunks
    // rather than switching upload mechanisms.
    constexpr u32 MaxInstancesPerUpdateCall = 65536 / sizeof(GpuInstance);

    for (u32 model = 0; model < ModelCount; model++) {
        u32 count = static_cast<u32>(StagingInstances[model].size());
        if (count == 0) { continue; }

        auto buffer = Buffers.Get(InstanceBuffers[model][frame_index]);
        const GpuInstance* data = StagingInstances[model].data();

        u32 uploaded = 0;
        while (uploaded < count) {
            u32 chunk_count = std::min(count - uploaded, MaxInstancesPerUpdateCall);
            vkCmdUpdateBuffer(
                GRenderer.FrameContext.DrawCommand,
                buffer->Handle,
                static_cast<VkDeviceSize>(uploaded) * sizeof(GpuInstance),
                static_cast<VkDeviceSize>(chunk_count) * sizeof(GpuInstance),
                data + uploaded
            );
            uploaded += chunk_count;
        }
    }
}

void PropPass::Render() {
    OutPropData();

    DrawCallsLastFrame = 0;
    InstancesLastFrame = 0;
    if (Mode == DrawMode::Off) { return; }

    VkCommandBuffer& cmd = GRenderer.FrameContext.DrawCommand;
    u32 frame_index = GRenderer.FrameContext.FrameInFlightIndex;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PropPipeline);

    for (u32 model = 0; model < ModelCount; model++) {
        u32 count = static_cast<u32>(StagingInstances[model].size());
        if (count == 0) { continue; }

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &Buffers.Get(Models[model].VertexBuffer)->Handle, &offset);
        vkCmdBindIndexBuffer(cmd, Buffers.Get(Models[model].IndexBuffer)->Handle, 0, VK_INDEX_TYPE_UINT32);

        std::array<VkDescriptorSet, 2> sets_to_bind = {
            GRenderer.GlobalDescriptors.Sets[frame_index],  // Set 0
            DescriptorSets[model][frame_index]              // Set 1
        };
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            PropPipelineLayout,
            DescriptorMap::Global::SetIndex,
            static_cast<u32>(sets_to_bind.size()),
            sets_to_bind.data(),
            0,
            nullptr
        );

        // One draw per submesh (material): bark and leaves of one tree are two instanced draws.
        for (const SubMeshResource& sub_mesh : Models[model].SubMeshes) {
            PushConstants push_constants { sub_mesh.Tint, sub_mesh.TextureLayer | (sub_mesh.Foliage ? FoliageFlag : 0u) };
            vkCmdPushConstants(cmd, PropPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push_constants);

            if (Mode == DrawMode::PerInstance) {
                for (u32 i = 0; i < count; i++) {
                    vkCmdDrawIndexed(cmd, sub_mesh.IndexCount, 1, sub_mesh.FirstIndex, 0, i);
                }
                DrawCallsLastFrame += count;
            } else {
                vkCmdDrawIndexed(cmd, sub_mesh.IndexCount, count, sub_mesh.FirstIndex, 0, 0);
                DrawCallsLastFrame++;
            }
        }
        InstancesLastFrame += count;
    }
}

// Per-model instance counts - StagingInstances is already bucketed by FrameSensibleTransfers()
// every frame, so this just reads it.
void PropPass::OutPropData() {
    static const char* model_names[ModelCount] = { "Tree", "Rock" };
    static_assert(ModelCount == 2, "update model_names above if the PropModel enum changes");

    if (DebugPanel::BeginSection(DebugPanel::Section::Props)) {
        static const char* mode_names[] = { "Instanced", "Per-instance draws", "Off" };
        int mode = static_cast<int>(Mode);
        if (ImGui::Combo("Draw mode", &mode, mode_names, IM_ARRAYSIZE(mode_names))) {
            Mode = static_cast<DrawMode>(mode);
        }
        ImGui::SliderFloat("Draw distance (m)", &DrawDistance, 100.0f, 8000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::Text("Draw calls: %u", DrawCallsLastFrame);

        u32 total = 0;
        for (u32 model = 0; model < ModelCount; model++) {
            u32 count = static_cast<u32>(StagingInstances[model].size());
            ImGui::Text("%s: %u (%zu submeshes)", model_names[model], count, Models[model].SubMeshes.size());
            total += count;
        }
        ImGui::Text("Total: %u (max %u/model)", total, MaxInstancesPerModel);

        DebugPanel::EndSection();
    }
}

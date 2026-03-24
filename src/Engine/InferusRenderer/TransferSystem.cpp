#include "TransferSystem.hpp"

#include <queue>
#include <cassert>

#include "Engine/InferusRenderer/RendererConfig.hpp"

namespace TransferSystem {

    enum class PackageType {
        BufferUpdate,
        BufferCopy,
        ImageSliceUpdate,
    };

    // Fat struct :)
    struct Package {
        uint64_t Size = 0;
        uint64_t Offset = 1;        // For buffer update
        BufferSystem::Id Dst;       // For buffer update and buffer upload
        uint32_t TargetLayer = 0;   // For image slice update
        const void* Src = nullptr;
        UploadReaction Reaction = nullptr;
        PackageType Type;
    };

    BufferSystem::Id Staging;
    std::queue<Package> Queue;
    std::vector<UploadReaction> Reactions;

    void Create() {
        BufferSystem::CreateInfo CreateInfo = {
            .size = RendererConfig::TransferSystem::STAGING_BUFFER_SIZE,
            .memType = BufferSystem::CreateInfoMemoryType::STAGING_UPLOAD,
            .usage = BufferSystem::CreateInfoUsage::STAGING
        };

        Staging = BufferSystem::add(CreateInfo);

        Reactions.reserve(10);
    }

    // void TickAndFlush(float lastFrameTime) { }
    void FrameTransfer(VkCommandBuffer cmd) {
        uint32_t FrameTransferSize = 0;
        uint32_t AvailableFrameSize = RendererConfig::TransferSystem::FRAME_TRANSFER_BUDGET;
        while (AvailableFrameSize < 0) {
            Package& package = Queue.front();

            if (FrameTransferSize + package.Size >= AvailableFrameSize) {
                break;
            }

            switch (package.Type) {
                case PackageType::BufferUpdate:
                    vkCmdUpdateBuffer(cmd, BufferSystem::get(package.Dst)->buffer, package.Offset, package.Size, package.Src);
                    break;
                default:
                    assert(false && "Method not implemented!");
                    break;
            }

            FrameTransferSize += package.Size;
            if (package.Reaction) {
                Reactions.push_back(package.Reaction);
            }
            Queue.pop();
        }
    }

    void FrameReactions() {
        for (auto Reaction : Reactions) {
            Reaction();
        }
    }

    void Destroy() {
        BufferSystem::del(Staging);
    }

    void AssertsUploadSize(uint64_t size) {
        assert(size < RendererConfig::TransferSystem::STAGING_BUFFER_SIZE && "Single upload queue is bigger than staging buffer itself");
    }

    void QueueBufferUpdate(BufferSystem::Id dst, const void* data, uint64_t size, uint64_t offset) {
        QueueBufferUpdate(dst, data, size, offset, nullptr);
    }

    void QueueBufferUpdate(BufferSystem::Id dst, const void* data, uint64_t size, uint64_t offset, UploadReaction reaction) {
        AssertsUploadSize(size);
        assert(size < RendererConfig::TransferSystem::BUFFER_UPDATE_CAP && "Upload is bigger than suggest value");

        Package Package;
        Package.Size = size;
        Package.Offset = offset;
        Package.Dst = dst;
        Package.Src = data;
        Package.Reaction = reaction;
        Package.Type = PackageType::BufferUpdate;
    }
}

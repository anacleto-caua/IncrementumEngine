#include "TransferSystem.hpp"

#include <queue>
#include <cstring>
#include <cassert>

#include "Engine/InferusRenderer/RendererConfig.hpp"

namespace TransferSystem {

    enum class PackageType {
        BufferUpdate,
        BufferCopy,
        ImageSliceUpdate,
    };

    namespace PackageData {
        struct BufferCopy {
            uint64_t ReadOffset = 0;
            uint64_t WriteOffset = 0;
            BufferSystem::Id DstBuffer;
            const void* Src = nullptr;
        };

        struct BufferUpdate {
            uint64_t WriteOffset = 0;
            BufferSystem::Id DstBuffer;
            const void* Src = nullptr;
        };

        struct ImageSliceUpdate {
            ImageSystem::Id DstImage;
            uint32_t TargetLayer = 0;
            uint64_t CopyOffset = 0;
        };

        union Data {
            BufferCopy BufferCopy;
            BufferUpdate BufferUpdate;
            ImageSliceUpdate ImageSliceUpdate;
        };
    }

    struct Package {
        PackageType Type;
        UploadReaction Reaction = nullptr;
        uint64_t Size = 0;
        PackageData::Data Data;
    };

    namespace RingBuffer {
        constexpr auto SIZE = RendererConfig::TransferSystem::STAGING_BUFFER_SIZE;

        BufferSystem::Id Buffer;
        uint8_t* MappedHead;
        uint8_t* Head;
        uint8_t* Tail;

        void Create() {
            BufferSystem::CreateInfo CreateInfo = {
                .size = SIZE ,
                .memType = BufferSystem::CreateInfoMemoryType::STAGING_UPLOAD,
                .usage = BufferSystem::CreateInfoUsage::STAGING
            };

            Buffer = BufferSystem::add(CreateInfo);
            MappedHead = static_cast<uint8_t*>(BufferSystem::map(Buffer));
            Head = MappedHead;
            Tail = Head;
        }

        void Destroy() {
            BufferSystem::unmap(Buffer);
            BufferSystem::del(Buffer);
        }

        void Paste(PackageType type, const void* src, uint64_t upload_size) {
            assert(upload_size < SIZE && "Single queued upload is bigger than staging buffer itself");
            uint64_t neck_size = Head - (MappedHead + SIZE);
            if (upload_size <= neck_size) {
                memcpy(Head, src, upload_size);
                Head += upload_size;
            } else {
                uint64_t past_neck_upload_size = upload_size - neck_size;

                // Altought it could've been avoided by making a list of non-written actions and
                // saving their data to a dinamyc growing pool I just want to catch some bad usage.
                uint64_t pre_tail = Tail - (MappedHead);
                assert(pre_tail > past_neck_upload_size && "Transfer system staging ring buffer got cluttered");

                // Do not split images, it's messy, just discard the neck
                if (type == PackageType::ImageSliceUpdate) {
                    assert(pre_tail > upload_size && "Can't fit whole texture in staging buffer pre-tail");
                    memcpy(MappedHead, src, upload_size);
                    Head = MappedHead + upload_size;
                    return; // Look's like bad flow control
                }

                memcpy(Head, src, neck_size);
                memcpy(MappedHead, (static_cast<const uint8_t *>(src)+past_neck_upload_size), past_neck_upload_size);
                Head = MappedHead + past_neck_upload_size;
            }
        }

        void Pick(PackageType type, uint64_t package_size, uint64_t& upload_size1, uint64_t& offset, uint64_t& upload_size2) {
            uint64_t tail_neck_size = Tail - (MappedHead + SIZE);
            if (package_size <= tail_neck_size) {
                upload_size1 = package_size;
                offset = MappedHead - Tail;
                upload_size2 = 0;
                Tail += package_size;
            } else {
                // Special case to account for images not being split
                if (type == PackageType::ImageSliceUpdate) {
                    upload_size1 = package_size;
                    offset = 0;
                    Tail = MappedHead + package_size;
                    return; // Look's like bad flow control
                }
                uint64_t past_neck_upload_size = package_size - tail_neck_size;
                upload_size1 = tail_neck_size;
                offset =  MappedHead - Tail;
                upload_size2 = past_neck_upload_size;
                Tail = MappedHead + past_neck_upload_size;
            }
        }

    }

    std::queue<Package> Queue;
    std::vector<UploadReaction> Reactions;

    void Create() {
        RingBuffer::Create();
        Reactions.reserve(10);
    }

    // void TickAndFlush(float lastFrameTime) { }
    void FrameTransfer(VkCommandBuffer cmd) {
        uint32_t FrameTransferSize = 0;
        uint32_t AvailableFrameSize = RendererConfig::TransferSystem::FRAME_TRANSFER_BUDGET;
        while (AvailableFrameSize > 0) {

            if (Queue.empty()) {
                break;
            }

            Package& package = Queue.front();

            if (FrameTransferSize + package.Size >= AvailableFrameSize) {
                break;
            }

            switch (package.Type) {
                case PackageType::BufferUpdate: {
                    vkCmdUpdateBuffer(
                        cmd,
                        BufferSystem::get(package.Data.BufferUpdate.DstBuffer)->buffer,
                        package.Data.BufferUpdate.WriteOffset,
                        package.Size,
                        package.Data.BufferUpdate.Src
                    );
                    break;
                }
                case PackageType::BufferCopy: {
                    assert(false && "Transfer method not implemented! Buffer copy");
                    break;
                }
                case PackageType::ImageSliceUpdate: {
                    auto Dst = ImageSystem::get(package.Data.ImageSliceUpdate.DstImage);
                    uint64_t upload_size1;
                    uint64_t offset;
                    uint64_t upload_size2;
                    RingBuffer::Pick(package.Type, package.Size, upload_size1, offset, upload_size2);

                    VkBufferImageCopy buffer_image_copy_cmd = {
                        .bufferOffset = offset,
                        .bufferRowLength = 0,
                        .bufferImageHeight = 0,
                        .imageSubresource = {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .mipLevel = 0,
                            .baseArrayLayer = package.Data.ImageSliceUpdate.TargetLayer,
                            .layerCount = 1
                        },
                        .imageOffset = {0, 0, 0},
                        .imageExtent = { Dst->width, Dst->height, 1 }
                    };

                    vkCmdCopyBufferToImage(
                        cmd,
                        BufferSystem::get(RingBuffer::Buffer)->buffer,
                        Dst->image,
                        Dst->layout,
                        1, &buffer_image_copy_cmd
                    );
                    break;
                }
                default: {
                    assert(false && "Transfer method inexistent or not implemented!");
                    break;
                }
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
        Reactions.clear();
    }

    void Destroy() {
        while (!Queue.empty()) {
            Queue.pop();
        }
        Reactions.clear();
        RingBuffer::Destroy();
    }

    void QueueBufferUpdate(BufferSystem::Id dst, const void* data, uint64_t size, uint64_t offset) {
        QueueBufferUpdate(dst, data, size, offset, nullptr);
    }

    void QueueBufferUpdate(BufferSystem::Id dst, const void* data, uint64_t size, uint64_t offset, UploadReaction reaction) {
        assert(size < RendererConfig::TransferSystem::BUFFER_UPDATE_CAP && "Upload is bigger than suggest value");

        Package package {
            .Type = PackageType::BufferUpdate,
            .Reaction = reaction,
            .Size = size,
            .Data = {
                .BufferUpdate = {
                    .WriteOffset = offset,
                    .DstBuffer = dst,
                    .Src = data
                }
            }
        };

        Queue.push(package);
    }

    void QueueImageSliceUpdate(ImageSystem::Id dst, const void* data, uint32_t target_layer, uint64_t size) {
        QueueImageSliceUpdate(dst, data, target_layer, size, nullptr);
    }

    void QueueImageSliceUpdate(ImageSystem::Id dst, const void* data, uint32_t target_layer, uint64_t size, UploadReaction reaction) {
        RingBuffer::Paste(PackageType::ImageSliceUpdate, data, size);

        Package package {
            .Type = PackageType::ImageSliceUpdate,
            .Reaction = reaction,
            .Size = size,
            .Data = {
                .ImageSliceUpdate = {
                    .DstImage = dst,
                    .TargetLayer = target_layer,
                    .CopyOffset = 0
                }
            }
        };

        Queue.push(package);
    }

}

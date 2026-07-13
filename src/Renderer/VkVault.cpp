#include "VkVault.hpp"

#include <array>
#include <string>
#include <format>
#include <cstring>

#include "Engine/Core/Window.hpp"

#ifdef NDEBUG
    constexpr bool EnableValidadtionLayers_ = false;
#else
    constexpr bool EnableValidadtionLayers_ = true;
#endif

namespace VkVault {
    static constexpr std::array<const char*, 5> DEVICE_EXTENSIONS = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
            // VMA extensions
            VK_EXT_MEMORY_BUDGET_EXTENSION_NAME, VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME
        };

    static constexpr std::array<const char*, 1> INSTANCE_EXTENSIONS = {
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
    };

#ifndef NDEBUG
    VkDebugUtilsMessengerEXT DebugMessenger_;

    static constexpr std::array<const char*, 1> VALIDATION_LAYERS = { "VK_LAYER_KHRONOS_validation" };
    static constexpr std::array<const char*, 1> VALIDATION_LAYERS_EXTENSION = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };

    VKAPI_ATTR VkBool32 VKAPI_CALL _DebugMessageCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT msg_severity,
            VkDebugUtilsMessageTypeFlagsEXT msg_type,
            const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
            [[maybe_unused]]void *user_data)
    {
        std::string msg_type_text;
        if (msg_type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)     msg_type_text += "General|";
        if (msg_type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)  msg_type_text += "Validation|";
        if (msg_type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) msg_type_text += "Performance|";

        // Result: [Validation] ID: 0x12345 | Message: ...
        std::string msg = std::format("[{}] ID: {} | {}",
            msg_type_text,
            callback_data->pMessageIdName ? callback_data->pMessageIdName : "None",
            callback_data->pMessage);

        switch (msg_severity) {
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
                analog::error("{}", msg);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
                analog::warn("{}", msg);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
                analog::info("{}", msg);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
                analog::debug("{}", msg);
                break;
            default:
                analog::critical("Unknown Severity Validation Error: {}", msg);
                break;
        }
        return VK_FALSE;
    }

    IncResult _SetupDebugMessenger() {
        VkDebugUtilsMessengerCreateInfoEXT create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        create_info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        create_info.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        create_info.pfnUserCallback = _DebugMessageCallback;

        auto func =
            (PFN_vkCreateDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(
                    Instance,
                    "vkCreateDebugUtilsMessengerEXT"
                );
        if (func == nullptr) {
            return IncResult::FAIL;
        }

        VK_CHECK(
            func(Instance, &create_info, nullptr, &DebugMessenger_),
            "debug messenger creation failed"
        );

        return IncResult::SUCCESS;
    }

    void _DestroyDebugUtilsMessengerEXT() {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT");

        if (func != nullptr) {
            func(Instance, DebugMessenger_, nullptr);
        }
    }
#endif

    VkSurfaceFormatKHR DESIRABLE_SURFACE_FORMAT = {
        .format = VK_FORMAT_B8G8R8A8_SRGB,
        .colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR
    };

    std::array<VkPresentModeKHR, 3> PRESENT_MODE_TIERLIST = {
        VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR
    };

    IncResult CreateInstance() {
        VkApplicationInfo app_info = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "Incrementum Renderer",
            .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
            .pEngineName = "Incrementum Engine",
            .engineVersion = VK_MAKE_VERSION(0, 0, 1),
            .apiVersion = VK_API_VERSION_1_4
        };

        std::vector<const char*> all_instance_extensions;
        all_instance_extensions.insert(all_instance_extensions.end(), INSTANCE_EXTENSIONS.begin(), INSTANCE_EXTENSIONS.end());

        std::vector<const char*> WindowExts = Window::GetRequiredExtensions();
        all_instance_extensions.insert(all_instance_extensions.end(), WindowExts.begin(), WindowExts.end());

        VkInstanceCreateInfo instance_create_info {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = &app_info,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<u32>(all_instance_extensions.size()),
            .ppEnabledExtensionNames = all_instance_extensions.data()
        };

        // Validation layers
        if constexpr (EnableValidadtionLayers_) {
            all_instance_extensions.insert(
                all_instance_extensions.end(),
                VALIDATION_LAYERS_EXTENSION.begin(),
                VALIDATION_LAYERS_EXTENSION.end()
            );
            instance_create_info.enabledLayerCount = static_cast<u32>(VALIDATION_LAYERS.size());
            instance_create_info.ppEnabledLayerNames = VALIDATION_LAYERS.data();
        }

        instance_create_info.enabledExtensionCount = static_cast<u32>(all_instance_extensions.size());
        instance_create_info.ppEnabledExtensionNames = all_instance_extensions.data();

        // Create Vulkan instance
        VK_CHECK(
            vkCreateInstance(&instance_create_info, nullptr, &Instance),
            "instance creation failed"
        );

        return IncResult::SUCCESS;
    }

    IncResult PickPhysicalDevice() {
        u32 physical_devices_count;
        vkEnumeratePhysicalDevices(Instance, &physical_devices_count, nullptr);
        std::vector<VkPhysicalDevice> physical_devices(physical_devices_count);
        vkEnumeratePhysicalDevices(Instance, &physical_devices_count, physical_devices.data());

        i32 king_of_the_hill_score = -1;
        VkPhysicalDevice king_of_the_hill_device;
        for (VkPhysicalDevice current_physical_device : physical_devices) {
            i32 score = 0;

            // Are required extensions supported
            u32 extension_count;
            vkEnumerateDeviceExtensionProperties(
                current_physical_device,
                nullptr,
                &extension_count,
                nullptr
            );

            std::vector<VkExtensionProperties> available_extensions(extension_count);
            vkEnumerateDeviceExtensionProperties(
                current_physical_device,
                nullptr,
                &extension_count,
                available_extensions.data()
            );

            // Are needed features available
            VkPhysicalDeviceFeatures device_features;
            vkGetPhysicalDeviceFeatures(current_physical_device, &device_features);

            // Format needed for depth buffer
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(current_physical_device, RendererConfig::DepthBuffer::Format, &properties);
            bool depth_format_support = (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
            if (!depth_format_support) {
                break;
            }

            // Apply preference for GPUs and higher resolution
            VkPhysicalDeviceProperties device_properties;
            vkGetPhysicalDeviceProperties(current_physical_device, &device_properties);

            if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                score += 1000;
            } else if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                score += 100;
            }

            score += device_properties.limits.maxImageDimension2D;

            bool extension_found;
            for (const char* extension : DEVICE_EXTENSIONS) {
                extension_found = false;
                for (const VkExtensionProperties &extension_properties : available_extensions) {
                    if(std::strcmp(extension, extension_properties.extensionName) == 0) {
                        extension_found = true;
                        break;
                    }
                }

                if(!extension_found) {
                    score = -1;
                    break;
                }
            }

            // Fight for Koth
            if (score > king_of_the_hill_score) {
                king_of_the_hill_score = score;
                king_of_the_hill_device = current_physical_device;
            }
        }

        if(king_of_the_hill_score < 0) {
            return IncResult::FAIL;
        }

        // Selecting physical device
        PhysicalDevice = king_of_the_hill_device;
        return IncResult::SUCCESS;
    }

    IncResult PickQueues() {
        struct QueueRequest {
            QueueContext *QueueCtx;
            i32 LatestScore;
            VkQueueFlags RequiredFlags;
            VkQueueFlags AvoidedFlags;
            bool NeedsPresent;
            bool ScoreUniqueness;
        };

        std::array<QueueRequest, Queues.size()> queue_requests;
        queue_requests[0] = {
            .QueueCtx = &Graphics,
            .LatestScore = -1,
            .RequiredFlags = VK_QUEUE_GRAPHICS_BIT,
            .AvoidedFlags = 0,
            .NeedsPresent = false,
            .ScoreUniqueness = true
        };
        queue_requests[1] = {
            .QueueCtx = &Present,
            .LatestScore = -1,
            .RequiredFlags = 0,
            .AvoidedFlags = VK_QUEUE_COMPUTE_BIT,       // Avoid since I don't explicitly support graphics->compute->present yet
            .NeedsPresent = true,
            .ScoreUniqueness = false
        };
        queue_requests[2] = {
            .QueueCtx = &Transfer,
            .LatestScore = -1,
            .RequiredFlags = VK_QUEUE_TRANSFER_BIT,
            .AvoidedFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
            .NeedsPresent = false,
            .ScoreUniqueness = true
        };
        queue_requests[3] = {
            .QueueCtx = &Compute,
            .LatestScore = -1,
            .RequiredFlags = VK_QUEUE_COMPUTE_BIT,
            .AvoidedFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT,
            .NeedsPresent = false,
            .ScoreUniqueness = true
        };

        u32 queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> all_queue_family_properties(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &queue_family_count, all_queue_family_properties.data());

        for (QueueRequest& req : queue_requests) {
            constexpr u32 SCORE_PER_UNIQUENESS = 1000;
            constexpr u32 SCORE_FOR_DESIRED_SUPPORT = 1000;
            constexpr u32 SCORE_PER_AVOIDED_FLAG = 100;

            for (u32 QueueFamilyIdx = 0; QueueFamilyIdx < queue_family_count; QueueFamilyIdx++) {
                VkQueueFamilyProperties QueueProperties = all_queue_family_properties[QueueFamilyIdx];

                i32 Score = 0;
                if (req.NeedsPresent) {
                    VkBool32 PresentSupport;
                    vkGetPhysicalDeviceSurfaceSupportKHR(PhysicalDevice, QueueFamilyIdx, Surface, &PresentSupport);
                    if (!PresentSupport) {
                        continue;
                    }
                    Score += SCORE_FOR_DESIRED_SUPPORT;
                }

                if ((QueueProperties.queueFlags & req.RequiredFlags) != req.RequiredFlags) {
                    Score = -1;
                    continue;
                }

                if ((QueueProperties.queueFlags & req.AvoidedFlags) == 0) {
                    Score += SCORE_PER_AVOIDED_FLAG;
                }

                if (req.ScoreUniqueness) {
                    for (QueueRequest &req2 : queue_requests) {
                        if (req2.LatestScore < 0) { break; }    // Do not compare to not yet picked queues
                        if (QueueFamilyIdx != req2.QueueCtx->Index) {
                            Score += SCORE_PER_UNIQUENESS;
                        }
                    }
                }

                if (Score > req.LatestScore) {
                    req.QueueCtx->Index = QueueFamilyIdx;
                    req.LatestScore = Score;
                }
            }
        }

        for (QueueRequest req : queue_requests) {
            if (req.LatestScore < 0) {
                return IncResult::FAIL;
            }
        }

        // Track unique queues
        std::vector<QueueContext*> same_queue_different_context;
        for (QueueContext* queue : Queues) {
            bool is_unique = true;
            for (QueueContext* q2 : UniqueQueues) {
                if (queue == q2) {
                    break;
                }
                if (queue->Index == q2->Index) {
                    is_unique = false;
                    same_queue_different_context.push_back(q2);
                }
            }
            if (is_unique) {
                queue->ResourceIndex = static_cast<u32>(UniqueQueues.size());
                UniqueQueues.push_back(queue);
                for (QueueContext* q2 : same_queue_different_context) {
                    q2->ResourceIndex = queue->ResourceIndex;
                }
            }
            same_queue_different_context.clear();
        }

        // out queue info for debug
        // TODO: make this available at the debug ui since vulkan uses queue indexes

        auto out_queue = [](const char* fancy_name, QueueContext& queue){
            analog::info("Queue: {}", fancy_name);
            analog::info(" - Index: {}", queue.Index);
            analog::info(" - Resource Index: {}", queue.ResourceIndex);
        };

        analog::info("VkVault creation, queues defined: ");
        analog::info("Unique queue count: {}", UniqueQueues.size());
        out_queue("Graphics", Graphics);
        out_queue("Transfer", Transfer);
        out_queue("Present", Present);
        out_queue("Compute", Compute);

        return IncResult::SUCCESS;
    }

    IncResult CreateLogicalDevice() {
        f32 queue_priority = 1.0f;

        std::vector<VkDeviceQueueCreateInfo> queue_create_infos = {};
        queue_create_infos.reserve(UniqueQueues.size());

        for (QueueContext *queue : UniqueQueues) {
            VkDeviceQueueCreateInfo create_info = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .queueFamilyIndex = queue->Index,
                .queueCount = 1,
                .pQueuePriorities = &queue_priority
            };
            queue_create_infos.push_back(create_info);
        }

        VkPhysicalDeviceFeatures device_features{};
        device_features.samplerAnisotropy = VK_TRUE;
        device_features.sampleRateShading = VK_TRUE;

        VkPhysicalDeviceFeatures2 device_features_2{};
        device_features_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        device_features_2.features = device_features;

        VkPhysicalDeviceSynchronization2Features sync_2_features{};
        sync_2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
        sync_2_features.synchronization2 = VK_TRUE;

        VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features{};
        dynamic_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
        dynamic_rendering_features.dynamicRendering = VK_TRUE;

        VkPhysicalDeviceTimelineSemaphoreFeatures timeline_features = {};
        timeline_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
        timeline_features.timelineSemaphore = VK_TRUE;

        device_features_2.pNext= &sync_2_features;
        sync_2_features.pNext = &dynamic_rendering_features;
        dynamic_rendering_features.pNext = &timeline_features;
        timeline_features.pNext = nullptr;

        VkDeviceCreateInfo device_create_info{};
        device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.pEnabledFeatures = VK_NULL_HANDLE;
        device_create_info.queueCreateInfoCount = static_cast<u32>(queue_create_infos.size());
        device_create_info.pQueueCreateInfos = queue_create_infos.data();
        device_create_info.enabledExtensionCount = static_cast<u32>(DEVICE_EXTENSIONS.size());
        device_create_info.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();
        device_create_info.pNext = &device_features_2;

        VK_CHECK(
            vkCreateDevice(PhysicalDevice, &device_create_info, nullptr, &Device),
            "device creation failed."
        );

        return IncResult::SUCCESS;
    }

    IncResult CreateVmaAllocator() {
        VmaAllocatorCreateInfo allocator_create_info {};
        allocator_create_info.physicalDevice = PhysicalDevice;
        allocator_create_info.device = Device;
        allocator_create_info.instance = Instance;

        VK_CHECK(
            vmaCreateAllocator(&allocator_create_info, &VmaAllocator),
            "vma allocator creation failed."
        );

        return IncResult::SUCCESS;
    }

    IncResult CreateQueuesResource() {
        QueueResources.Initialize();

        // Create the Queues resources
        VkCommandPoolCreateInfo cmd_pool_create_info {};
        cmd_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmd_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        for (QueueContext* queue : Queues) {
            vkGetDeviceQueue(Device, queue->Index, 0, &queue->Queue);

            if (QueueResources[queue].MainCmdPool == VK_NULL_HANDLE) {
                auto& r = QueueResources[queue];
                cmd_pool_create_info.queueFamilyIndex = queue->Index;
                VK_CHECK(
                    vkCreateCommandPool(Device, &cmd_pool_create_info, nullptr, &r.MainCmdPool),
                    "main command pool creation failed"
                );
            }
        }
        return IncResult::SUCCESS;
    }

    void PickSurfaceFormat() {
        u32 SurfaceFormatCount;
        std::vector<VkSurfaceFormatKHR> SurfaceFormats;
        vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &SurfaceFormatCount, nullptr);
        SurfaceFormats.resize(SurfaceFormatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &SurfaceFormatCount, SurfaceFormats.data());

        bool found = false;
        for (VkSurfaceFormatKHR CurrSurfaceFormat : SurfaceFormats) {
            if ((CurrSurfaceFormat.format == DESIRABLE_SURFACE_FORMAT.format) &&
                (CurrSurfaceFormat.colorSpace == DESIRABLE_SURFACE_FORMAT.colorSpace)) {
                // Desirable surface format picked
                SurfaceFormat = CurrSurfaceFormat;
                found = true;
                break;
            }
        }

        // Non desirable surface format picked.
        if (!found) {
            SurfaceFormat = SurfaceFormats[0];
            analog::warn("non desirable surface format was picked");
        }

        // Remember to fill in the collor attachment formats
        ColorAttachmentFormats[0] = SurfaceFormat.format;
    }

    void PickPresentMode() {
        u32 present_mode_count;
        std::vector<VkPresentModeKHR> present_modes;
        vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &present_mode_count, nullptr);
        present_modes.resize(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &present_mode_count, present_modes.data());

        for (VkPresentModeKHR comparing_present_mode : PRESENT_MODE_TIERLIST ) {
            for (VkPresentModeKHR current_present_mode : present_modes) {
                if (comparing_present_mode == current_present_mode) {
                    // Desirable present mode picked
                    PresentMode = current_present_mode;
                    return;
                }
            }
        }
        // Non desirable present mode picked
        PresentMode = present_modes[0];
        analog::warn("non desirable present mode was picked");
    }

    IncResult Create() {
        CreateInstance();

        if constexpr (EnableValidadtionLayers_) {
            _SetupDebugMessenger();
        }

        PickPhysicalDevice();

        if (!Window::CreateSurface(Instance, Surface)) {
            analog::error("couldn't create window surface for vulkan usage");
            return IncResult::FAIL;
        }

        PickSurfaceFormat();
        PickPresentMode();
        PickQueues();
        CreateLogicalDevice();
        CreateQueuesResource();
        CreateVmaAllocator();
        return IncResult::SUCCESS;
    }

    void Destroy() {
        vkDeviceWaitIdle(Device);

        for (QueueResourcePool r :  QueueResources) {
            if (r.MainCmdPool) { vkDestroyCommandPool(Device, r.MainCmdPool, nullptr); }
        }

        if (VmaAllocator) { vmaDestroyAllocator(VmaAllocator); }
        if (Device) { vkDestroyDevice(Device, nullptr); }
        if (Surface) { vkDestroySurfaceKHR(Instance, Surface, nullptr); }

        if constexpr (EnableValidadtionLayers_) {
            _DestroyDebugUtilsMessengerEXT();
        }

        if (Instance) { vkDestroyInstance(Instance, nullptr); }
    }

    VkCommandBuffer SingleTimeCmdBegin(QueueContext& ctx) {
        VkCommandBufferAllocateInfo cmd_buffer_alloc_info {};
        cmd_buffer_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_buffer_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_buffer_alloc_info.commandPool = QueueResources[&ctx].MainCmdPool;
        cmd_buffer_alloc_info.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(Device, &cmd_buffer_alloc_info, &cmd);

        VkCommandBufferBeginInfo cmd_buffer_begin_info{};
        cmd_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmd_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &cmd_buffer_begin_info);

        return cmd;
    }

    void SingleTimeCmdSubmit(QueueContext& ctx, VkCommandBuffer cmd) {
        vkEndCommandBuffer(cmd);

        VkSubmitInfo cmd_buffer_submit_info{};
        cmd_buffer_submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        cmd_buffer_submit_info.commandBufferCount = 1;
        cmd_buffer_submit_info.pCommandBuffers = &cmd;
        vkQueueSubmit(ctx.Queue, 1, &cmd_buffer_submit_info, VK_NULL_HANDLE);

        vkQueueWaitIdle(ctx.Queue);

        vkFreeCommandBuffers(Device, QueueResources[&ctx].MainCmdPool, 1, &cmd);
    }

    VkSurfaceCapabilitiesKHR QuerySurfaceCapabilities() {
        VkSurfaceCapabilitiesKHR capabilities {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysicalDevice, Surface, &capabilities);
        return capabilities;
    }

}

#include "VulkanContext.hpp"

#include <array>
#include <string>

#include <spdlog/spdlog.h>

#include "Engine/Core/Window.hpp"
#include "Engine/InferusRenderer/RendererConfig.hpp"

namespace VulkanContext {
    static constexpr std::array<const char*, 5> DEVICE_EXTENSIONS = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
            // VMA extensions
            VK_EXT_MEMORY_BUDGET_EXTENSION_NAME, VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME
        };

    static constexpr std::array<const char*, 1> INSTANCE_EXTENSIONS = {
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
    };

#ifndef NDEBUG
    VkDebugUtilsMessengerEXT _DebugMessenger;

    static constexpr std::array<const char*, 1> VALIDATION_LAYERS = { "VK_LAYER_KHRONOS_validation" };
    static constexpr std::array<const char*, 1> VALIDATION_LAYERS_EXTENSION = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };

    VKAPI_ATTR VkBool32 VKAPI_CALL _DebugMessageCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
            [[maybe_unused]]void *pUserData)
    {
        std::string strMessageType;
        if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)     strMessageType += "General|";
        if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)  strMessageType += "Validation|";
        if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) strMessageType += "Performance|";

        // Result: [Validation] ID: 0x12345 | Message: ...
        std::string msg = fmt::format("[{}] ID: {} | {}",
            strMessageType,
            pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "None",
            pCallbackData->pMessage);

        switch (messageSeverity) {
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
                spdlog::error(msg);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
                spdlog::warn(msg);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
                spdlog::info(msg);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
                spdlog::debug(msg);
                break;
            default:
                spdlog::critical("Unknown Severity Validation Error: {}", msg);
                break;
        }
        return VK_FALSE;
    }

    InferusResult _SetupDebugMessenger() {
        VkDebugUtilsMessengerCreateInfoEXT CreateInfo{};
        CreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        CreateInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        CreateInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        CreateInfo.pfnUserCallback = _DebugMessageCallback;

        auto Func =
            (PFN_vkCreateDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(
                    Instance,
                    "vkCreateDebugUtilsMessengerEXT"
                );
        if ((Func == nullptr) || (Func(Instance, &CreateInfo, nullptr, &_DebugMessenger) != VK_SUCCESS)) {
            return InferusResult::FAIL;
        }
        return InferusResult::SUCCESS;
    }

    void _DestroyDebugUtilsMessengerEXT() {
        auto Func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT");

        if (Func != nullptr) {
            Func(Instance, _DebugMessenger, nullptr);
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

    InferusResult CreateInstance() {
        VkApplicationInfo AppInfo {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "Inferus Renderer",
            .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
            .pEngineName = "Inferus Engine",
            .engineVersion = VK_MAKE_VERSION(0, 0, 1),
            .apiVersion = VK_API_VERSION_1_4
        };

        std::vector<const char*> AllInstanceExtensions;
        AllInstanceExtensions.insert(AllInstanceExtensions.end(), INSTANCE_EXTENSIONS.begin(), INSTANCE_EXTENSIONS.end());

        std::vector<const char*> WindowExts = Window::GetRequiredExtensions();
        AllInstanceExtensions.insert(AllInstanceExtensions.end(), WindowExts.begin(), WindowExts.end());

        VkInstanceCreateInfo InstanceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = &AppInfo,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<uint32_t>(AllInstanceExtensions.size()),
            .ppEnabledExtensionNames = AllInstanceExtensions.data()
        };

        // Validation layers
#ifndef NDEBUG
        AllInstanceExtensions.insert(
            AllInstanceExtensions.end(),
            VALIDATION_LAYERS_EXTENSION.begin(),
            VALIDATION_LAYERS_EXTENSION.end()
        );
        InstanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
        InstanceCreateInfo.ppEnabledLayerNames = VALIDATION_LAYERS.data();
#endif

        InstanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(AllInstanceExtensions.size());
        InstanceCreateInfo.ppEnabledExtensionNames = AllInstanceExtensions.data();

        // Create Vulkan instance
        if (vkCreateInstance(&InstanceCreateInfo, nullptr, &Instance) != VK_SUCCESS) {
            return InferusResult::FAIL;
        }
        return InferusResult::SUCCESS;
    }

    InferusResult PickPhysicalDevice() {
        uint32_t PhysicalDevicesCount;
        vkEnumeratePhysicalDevices(Instance, &PhysicalDevicesCount, nullptr);
        std::vector<VkPhysicalDevice> PhysicalDevices(PhysicalDevicesCount);
        vkEnumeratePhysicalDevices(Instance, &PhysicalDevicesCount, PhysicalDevices.data());

        int32_t KingOfTheHillScore = -1;
        VkPhysicalDevice KingOfTheHillDevice;
        for (VkPhysicalDevice CurrentPhysicalDevice : PhysicalDevices) {
            int32_t Score = 0;

            // Are required extensions supported
            uint32_t ExtensionCount;
            vkEnumerateDeviceExtensionProperties(
                CurrentPhysicalDevice,
                nullptr,
                &ExtensionCount,
                nullptr
            );
            std::vector<VkExtensionProperties> AvailableExtensions(ExtensionCount);
            vkEnumerateDeviceExtensionProperties(
                CurrentPhysicalDevice,
                nullptr,
                &ExtensionCount,
                AvailableExtensions.data()
            );

            // Are needed features available
            VkPhysicalDeviceFeatures DeviceFeatures;
            vkGetPhysicalDeviceFeatures(CurrentPhysicalDevice, &DeviceFeatures);

            // Format needed for depth buffer
            VkFormatProperties Properties;
            vkGetPhysicalDeviceFormatProperties(CurrentPhysicalDevice, RendererConfig::DepthBuffer::Format, &Properties);
            bool DepthFormatSupport = (Properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
            if (!DepthFormatSupport) {
                break;
            }

            // Apply preference for GPUs and higher resolution
            VkPhysicalDeviceProperties DeviceProperties;
            vkGetPhysicalDeviceProperties(CurrentPhysicalDevice, &DeviceProperties);

            if (DeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                Score += 1000;
            } else if (DeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                Score += 100;
            }

            Score += DeviceProperties.limits.maxImageDimension2D;

            bool ExtensionFound;
            for (const char* Extension : DEVICE_EXTENSIONS) {
                ExtensionFound = false;
                for (const VkExtensionProperties &ExtensionProperties : AvailableExtensions) {
                    if(std::strcmp(Extension, ExtensionProperties.extensionName) == 0) {
                        ExtensionFound = true;
                        break;
                    }
                }

                if(!ExtensionFound) {
                    Score = -1;
                    break;
                }
            }

            // Fight for Koth
            if (Score > KingOfTheHillScore) {
                KingOfTheHillScore = Score;
                KingOfTheHillDevice = CurrentPhysicalDevice;
            }
        }

        if(KingOfTheHillScore < 0) {
            return InferusResult::FAIL;
        }

        // Selecting physical device
        PhysicalDevice = KingOfTheHillDevice;
        return InferusResult::SUCCESS;
    }

    InferusResult PickQueues() {
        struct QueueRequest {
            QueueContext *QueueCtx;
            int32_t LatestScore;
            VkQueueFlags RequiredFlags;
            VkQueueFlags AvoidedFlags;
            bool NeedsPresent;
            bool ScoreUniqueness;
        };

        std::array<QueueRequest, 4> QueueRequests;
        QueueRequests[0] = {
            .QueueCtx = &Graphics,
            .LatestScore = -1,
            .RequiredFlags = VK_QUEUE_GRAPHICS_BIT,
            .AvoidedFlags = 0,
            .NeedsPresent = false,
            .ScoreUniqueness = true
        };
        QueueRequests[1] = {
            .QueueCtx = &Present,
            .LatestScore = -1,
            .RequiredFlags = 0,
            .AvoidedFlags = VK_QUEUE_COMPUTE_BIT,       // Avoid since I don't explicitly support graphics->compute->present yet
            .NeedsPresent = true,
            .ScoreUniqueness = false
        };
        QueueRequests[2] = {
            .QueueCtx = &Transfer,
            .LatestScore = -1,
            .RequiredFlags = VK_QUEUE_TRANSFER_BIT,
            .AvoidedFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
            .NeedsPresent = false,
            .ScoreUniqueness = true
        };
        QueueRequests[3] = {
            .QueueCtx = &Compute,
            .LatestScore = -1,
            .RequiredFlags = VK_QUEUE_COMPUTE_BIT,
            .AvoidedFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT,
            .NeedsPresent = false,
            .ScoreUniqueness = true
        };

        uint32_t QueueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &QueueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> AllQueueFamiliesProperties(QueueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &QueueFamilyCount, AllQueueFamiliesProperties.data());

        for (QueueRequest& Req : QueueRequests) {
            constexpr uint32_t SCORE_PER_UNIQUENESS = 1000;
            constexpr uint32_t SCORE_FOR_DESIRED_SUPPORT = 1000;
            constexpr uint32_t SCORE_PER_AVOIDED_FLAG = 100;

            for (uint32_t QueueFamilyIdx = 0; QueueFamilyIdx < QueueFamilyCount; QueueFamilyIdx++) {
                VkQueueFamilyProperties QueueProperties = AllQueueFamiliesProperties[QueueFamilyIdx];

                int32_t Score = 0;
                if (Req.NeedsPresent) {
                    VkBool32 PresentSupport;
                    vkGetPhysicalDeviceSurfaceSupportKHR(PhysicalDevice, QueueFamilyIdx, Surface, &PresentSupport);
                    if (!PresentSupport) {
                        continue;
                    }
                    Score += SCORE_FOR_DESIRED_SUPPORT;
                }

                if ((QueueProperties.queueFlags & Req.RequiredFlags) != Req.RequiredFlags) {
                    Score = -1;
                    continue;
                }

                if ((QueueProperties.queueFlags & Req.AvoidedFlags) == 0) {
                    Score += SCORE_PER_AVOIDED_FLAG;
                }

                if (Req.ScoreUniqueness) {
                    for (QueueRequest &Req2 : QueueRequests) {
                        if (Req2.LatestScore < 0) { break; }    // Do not compare to not yet picked queues
                        if (QueueFamilyIdx != Req2.QueueCtx->Index) {
                            Score += SCORE_PER_UNIQUENESS;
                        }
                    }
                }

                if (Score > Req.LatestScore) {
                    Req.QueueCtx->Index = QueueFamilyIdx;
                    Req.LatestScore = Score;
                }
            }
        }

        for (QueueRequest Req : QueueRequests) {
            if (Req.LatestScore < 0) {
                return InferusResult::FAIL;
            }
        }

        return InferusResult::SUCCESS;
    }

    InferusResult CreateLogicalDevice() {
        float QueuePriority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> QueueCreateInfos = {};
        for (QueueContext *Queue :Queues) {
            bool IsUnique = true;
            for (auto CreateInfo : QueueCreateInfos) {
                if (Queue->Index == CreateInfo.queueFamilyIndex) {
                    IsUnique = false;
                    break;
                }
            }
            if (IsUnique) {
                VkDeviceQueueCreateInfo QueueCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .queueFamilyIndex = Queue->Index,
                    .queueCount = 1,
                    .pQueuePriorities = &QueuePriority
                };
                QueueCreateInfos.push_back(QueueCreateInfo);
            }
        }

        VkPhysicalDeviceFeatures DeviceFeatures{};
        DeviceFeatures.samplerAnisotropy = VK_TRUE;
        DeviceFeatures.sampleRateShading = VK_TRUE;

        VkPhysicalDeviceFeatures2 DeviceFeatures2{};
        DeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        DeviceFeatures2.features = DeviceFeatures;

        VkPhysicalDeviceSynchronization2Features Sync2Features{};
        Sync2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
        Sync2Features.synchronization2 = VK_TRUE;

        VkPhysicalDeviceDynamicRenderingFeatures DynamicRenderingFeatures{};
        DynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
        DynamicRenderingFeatures.dynamicRendering = VK_TRUE;

        DeviceFeatures2.pNext= &Sync2Features;
        Sync2Features.pNext = &DynamicRenderingFeatures;
        DynamicRenderingFeatures.pNext = nullptr;

        VkDeviceCreateInfo DeviceCreateInfo{};
        DeviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        DeviceCreateInfo.pEnabledFeatures = VK_NULL_HANDLE;
        DeviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(QueueCreateInfos.size());
        DeviceCreateInfo.pQueueCreateInfos = QueueCreateInfos.data();
        DeviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(DEVICE_EXTENSIONS.size());
        DeviceCreateInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();
        DeviceCreateInfo.pNext = &DeviceFeatures2;

        if (vkCreateDevice(PhysicalDevice, &DeviceCreateInfo, nullptr, &Device) != VK_SUCCESS) {
            return InferusResult::FAIL;
        }
        return InferusResult::SUCCESS;
    }

    InferusResult CreateVmaAllocator() {
        VmaAllocatorCreateInfo AllocatorCreateInfo = {};
        AllocatorCreateInfo.physicalDevice = PhysicalDevice;
        AllocatorCreateInfo.device = Device;
        AllocatorCreateInfo.instance = Instance;
        if (vmaCreateAllocator(&AllocatorCreateInfo, &VmaAllocator) != VK_SUCCESS) {
            return InferusResult::FAIL;
        }
        return InferusResult::SUCCESS;
    }

    InferusResult CreateQueuesCmdPool() {
        // Create the Queues
        VkCommandPoolCreateInfo PoolCreateInfo{};
        PoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        PoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        for (QueueContext *Queue : Queues) {
            vkGetDeviceQueue(Device, Queue->Index, 0, &Queue->Queue);
            PoolCreateInfo.queueFamilyIndex = Queue->Index;
            if (vkCreateCommandPool(Device, &PoolCreateInfo, nullptr, &Queue->MainCmdPool) != VK_SUCCESS) {
                spdlog::error("main command pool creation failed");
                return InferusResult::FAIL;
            }
        }
        return InferusResult::SUCCESS;
    }

    void PickSurfaceFormat() {
        uint32_t SurfaceFormatCount;
        std::vector<VkSurfaceFormatKHR> SurfaceFormats;
        vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &SurfaceFormatCount, nullptr);
        SurfaceFormats.resize(SurfaceFormatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &SurfaceFormatCount, SurfaceFormats.data());

        for (VkSurfaceFormatKHR CurrSurfaceFormat : SurfaceFormats) {
            if ((CurrSurfaceFormat.format == DESIRABLE_SURFACE_FORMAT.format) &&
                (CurrSurfaceFormat.colorSpace == DESIRABLE_SURFACE_FORMAT.colorSpace)) {
                // Desirable surface format picked
                SurfaceFormat = CurrSurfaceFormat;
                return;
            }
        }
        // Non desirable surface format picked.
        SurfaceFormat = SurfaceFormats[0];
    }

    void PickPresentMode() {
        uint32_t PresentModesCount;
        std::vector<VkPresentModeKHR> PresentModes;
        vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &PresentModesCount, nullptr);
        PresentModes.resize(PresentModesCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &PresentModesCount, PresentModes.data());

        for (VkPresentModeKHR ComparingPresentMode : PRESENT_MODE_TIERLIST ) {
            for (VkPresentModeKHR CurrPresentMode : PresentModes) {
                if (ComparingPresentMode == CurrPresentMode) {
                    // Desirable present mode picked
                    PresentMode = CurrPresentMode;
                    return;
                }
            }
        }
        // Non desirable present mode picked
        PresentMode = PresentModes[0];
    }

    InferusResult Create() {
        CreateInstance();

#ifndef NDEBUG
        _SetupDebugMessenger();
#endif

        PickPhysicalDevice();
        Window::CreateSurface(Instance, Surface);
        PickSurfaceFormat();
        PickPresentMode();
        PickQueues();
        CreateLogicalDevice();
        CreateQueuesCmdPool();
        CreateVmaAllocator();
        return InferusResult::SUCCESS;
    }

    void Destroy() {
        vkDeviceWaitIdle(Device);

        for (QueueContext *Queue : Queues) {
            if (Queue->MainCmdPool) { vkDestroyCommandPool(Device, Queue->MainCmdPool, nullptr); }
        }

        if (VmaAllocator) { vmaDestroyAllocator(VmaAllocator); }
        if (Device) { vkDestroyDevice(Device, nullptr); }
        if (Surface) { vkDestroySurfaceKHR(Instance, Surface, nullptr); }

#ifndef NDEBUG
        _DestroyDebugUtilsMessengerEXT();
#endif
        if (Instance) { vkDestroyInstance(Instance, nullptr); }
    }

    VkCommandBuffer SingleTimeCmdBegin(QueueContext& ctx) {
        VkCommandBufferAllocateInfo AllocInfo{};
        AllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        AllocInfo.commandPool = ctx.MainCmdPool;
        AllocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(Device, &AllocInfo, &cmd);

        VkCommandBufferBeginInfo BeginInfo{};
        BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &BeginInfo);

        return cmd;
    }

    void SingleTimeCmdSubmit(QueueContext& ctx, VkCommandBuffer cmd) {
        vkEndCommandBuffer(cmd);

        VkSubmitInfo SubmitInfo{};
        SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        SubmitInfo.commandBufferCount = 1;
        SubmitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(ctx.Queue, 1, &SubmitInfo, VK_NULL_HANDLE);

        vkQueueWaitIdle(ctx.Queue);

        vkFreeCommandBuffers(Device, ctx.MainCmdPool, 1, &cmd);
    }
}

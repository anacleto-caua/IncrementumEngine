#include "SystemInfo.hpp"

#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include "Renderer/VkVault.hpp"
#include "Engine/Core/WindowSDL.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <powrprof.h>
#else
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <set>
#include <unistd.h>
#include <sys/utsname.h>
#endif

namespace SystemInfo {
    const char* PresentModeName(VkPresentModeKHR mode) {
        switch (mode) {
            case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
            case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
            case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
            default: return "OTHER";
        }
    }

    const char* DeviceTypeName(VkPhysicalDeviceType type) {
        switch (type) {
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
            case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
            default: return "other";
        }
    }

    std::string VersionString(u32 version) {
        return std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
               std::to_string(VK_API_VERSION_MINOR(version)) + "." +
               std::to_string(VK_API_VERSION_PATCH(version));
    }

#if defined(_WIN32)
    void WriteOsFields(JsonWriter& json) {
        char cpu_name[256] = {};
        DWORD cpu_name_size = sizeof(cpu_name);
        RegGetValueA(
            HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            "ProcessorNameString", RRF_RT_REG_SZ, nullptr, cpu_name, &cpu_name_size
        );
        DWORD cpu_mhz = 0;
        DWORD cpu_mhz_size = sizeof(cpu_mhz);
        RegGetValueA(
            HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            "~MHz", RRF_RT_REG_DWORD, nullptr, &cpu_mhz, &cpu_mhz_size
        );

        // Physical cores and L3 size: walk the processor-core and cache relations.
        u32 physical_cores = 0;
        u64 l3_bytes = 0;
        DWORD info_size = 0;
        GetLogicalProcessorInformationEx(RelationAll, nullptr, &info_size);
        std::vector<u8> info_buffer(info_size);
        if (GetLogicalProcessorInformationEx(RelationAll, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(info_buffer.data()), &info_size)) {
            for (DWORD offset = 0; offset < info_size;) {
                auto* entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(info_buffer.data() + offset);
                if (entry->Relationship == RelationProcessorCore) {
                    physical_cores++;
                } else if (entry->Relationship == RelationCache && entry->Cache.Level == 3) {
                    l3_bytes += entry->Cache.CacheSize;
                }
                offset += entry->Size;
            }
        }

        MEMORYSTATUSEX memory_status = {};
        memory_status.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memory_status);

        // RtlGetVersion, not GetVersionEx: the latter lies about the version to unmanifested apps.
        using RtlGetVersionFn = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
        RTL_OSVERSIONINFOW os_version = {};
        os_version.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);
        if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
            auto rtl_get_version = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
            if (rtl_get_version) { rtl_get_version(&os_version); }
        }

        SYSTEM_POWER_STATUS power_status = {};
        GetSystemPowerStatus(&power_status);

        std::string power_scheme_name = "unknown";
        GUID* active_scheme = nullptr;
        if (PowerGetActiveScheme(nullptr, &active_scheme) == ERROR_SUCCESS) {
            wchar_t name_buffer[256] = {};
            DWORD name_size = sizeof(name_buffer);
            if (PowerReadFriendlyName(nullptr, active_scheme, nullptr, nullptr, reinterpret_cast<UCHAR*>(name_buffer), &name_size) == ERROR_SUCCESS) {
                char narrow[256] = {};
                WideCharToMultiByte(CP_UTF8, 0, name_buffer, -1, narrow, sizeof(narrow), nullptr, nullptr);
                power_scheme_name = narrow;
            }
            LocalFree(active_scheme);
        }

        char computer_name[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD computer_name_size = sizeof(computer_name);
        GetComputerNameA(computer_name, &computer_name_size);

        json.Field("hostname", computer_name);
        json.Field("os", std::string("Windows ") + std::to_string(os_version.dwMajorVersion) + "." +
                         std::to_string(os_version.dwMinorVersion) + " build " + std::to_string(os_version.dwBuildNumber));
        // The registry string is space-padded on some CPUs.
        std::string_view cpu_view(cpu_name);
        while (!cpu_view.empty() && cpu_view.back() == ' ') { cpu_view.remove_suffix(1); }
        json.Field("cpu", cpu_view);
        json.Field("cpu_base_mhz", static_cast<u32>(cpu_mhz));
        json.Field("cpu_physical_cores", physical_cores);
        json.Field("cpu_l3_mb", static_cast<f64>(l3_bytes) / (1024.0 * 1024.0));
        json.Field("ram_total_mb", static_cast<u64>(memory_status.ullTotalPhys / (1024 * 1024)));
        // ACLineStatus: 0 = battery, 1 = plugged in, 255 = unknown (desktops usually report 1).
        json.Field("ac_power", static_cast<u32>(power_status.ACLineStatus));
        json.Field("battery_percent", static_cast<u32>(power_status.BatteryLifePercent));
        json.Field("power_plan", power_scheme_name);
    }
#else
    // First line of a sysfs/procfs file, without the trailing newline; empty if it can't be read.
    std::string ReadLine(const std::string& path) {
        std::string line;
        if (FILE* file = std::fopen(path.c_str(), "r")) {
            char buffer[256];
            if (std::fgets(buffer, sizeof(buffer), file)) { line = buffer; }
            std::fclose(file);
        }
        while (!line.empty() && (line.back() == '\n' || line.back() == ' ')) { line.pop_back(); }
        return line;
    }

    // Cache sizes in sysfs read like "4096K" (or "8M").
    u64 ParseCacheSize(const std::string& text) {
        char* end = nullptr;
        u64 value = std::strtoull(text.c_str(), &end, 10);
        if (end && *end == 'K') { value *= 1024; }
        else if (end && *end == 'M') { value *= 1024 * 1024; }
        return value;
    }

    void WriteOsFields(JsonWriter& json) {
        std::string cpu_name = "unknown";
        if (FILE* cpuinfo = std::fopen("/proc/cpuinfo", "r")) {
            char line[512];
            while (std::fgets(line, sizeof(line), cpuinfo)) {
                std::string_view view(line);
                if (view.starts_with("model name")) {
                    auto colon = view.find(':');
                    if (colon != std::string_view::npos) {
                        cpu_name = std::string(view.substr(colon + 2));
                        if (!cpu_name.empty() && cpu_name.back() == '\n') { cpu_name.pop_back(); }
                    }
                    break;
                }
            }
            std::fclose(cpuinfo);
        }

        utsname name = {};
        uname(&name);

        u64 ram_bytes = static_cast<u64>(sysconf(_SC_PHYS_PAGES)) * static_cast<u64>(sysconf(_SC_PAGESIZE));

        // Physical cores and L3 size from sysfs topology: a core is a unique (package, core id) pair,
        // and each L3 instance is counted once, keyed by the set of CPUs sharing it. Offline CPUs have
        // no topology directory and are skipped.
        std::set<std::pair<std::string, std::string>> cores;
        std::set<std::string> l3_instances;
        u64 l3_bytes = 0;
        i64 configured_cpus = sysconf(_SC_NPROCESSORS_CONF);
        for (i64 cpu = 0; cpu < configured_cpus; cpu++) {
            std::string cpu_dir = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
            std::string core_id = ReadLine(cpu_dir + "/topology/core_id");
            if (core_id.empty()) { continue; }
            cores.insert({ReadLine(cpu_dir + "/topology/physical_package_id"), core_id});

            for (u32 index = 0;; index++) {
                std::string cache_dir = cpu_dir + "/cache/index" + std::to_string(index);
                std::string level = ReadLine(cache_dir + "/level");
                if (level.empty()) { break; }
                if (level != "3") { continue; }
                if (l3_instances.insert(ReadLine(cache_dir + "/shared_cpu_list")).second) {
                    l3_bytes += ParseCacheSize(ReadLine(cache_dir + "/size"));
                }
            }
        }

        // Linux has no direct equivalent of the registry's "~MHz" (the nominal clock). Best source
        // first: ACPI CPPC nominal_freq (MHz; AMD and recent Intel), then intel_pstate's
        // base_frequency (kHz), then cpuinfo_max_freq (kHz), which is the boost ceiling, not the base
        // clock. cpu_base_mhz_source records which one was used.
        u32 cpu_base_mhz = 0;
        std::string cpu_base_mhz_source = "unknown";
        std::string nominal_freq = ReadLine("/sys/devices/system/cpu/cpu0/acpi_cppc/nominal_freq");
        std::string base_frequency = ReadLine("/sys/devices/system/cpu/cpu0/cpufreq/base_frequency");
        std::string max_frequency = ReadLine("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
        if (std::atoll(nominal_freq.c_str()) > 0) {
            cpu_base_mhz = static_cast<u32>(std::atoll(nominal_freq.c_str()));
            cpu_base_mhz_source = "acpi_cppc/nominal_freq";
        } else if (std::atoll(base_frequency.c_str()) > 0) {
            cpu_base_mhz = static_cast<u32>(std::atoll(base_frequency.c_str()) / 1000);
            cpu_base_mhz_source = "cpufreq/base_frequency";
        } else if (std::atoll(max_frequency.c_str()) > 0) {
            cpu_base_mhz = static_cast<u32>(std::atoll(max_frequency.c_str()) / 1000);
            cpu_base_mhz_source = "cpufreq/cpuinfo_max_freq";
        }

        // Power supplies, mapped onto Windows' SYSTEM_POWER_STATUS convention: ac_power 0 = battery,
        // 1 = plugged in, 255 = unknown; battery_percent 255 = unknown / no battery. Batteries with
        // scope "Device" (mice, gamepads) are not the system battery and are skipped.
        u32 ac_power = 255;
        u32 battery_percent = 255;
        std::string battery_status;
        if (DIR* supplies = opendir("/sys/class/power_supply")) {
            while (dirent* entry = readdir(supplies)) {
                if (entry->d_name[0] == '.') { continue; }
                std::string supply_dir = std::string("/sys/class/power_supply/") + entry->d_name;
                std::string type = ReadLine(supply_dir + "/type");
                if (type == "Mains") {
                    std::string online = ReadLine(supply_dir + "/online");
                    if (!online.empty() && ac_power != 1) { ac_power = online == "1" ? 1 : 0; }
                } else if (type == "Battery" && battery_percent == 255 && ReadLine(supply_dir + "/scope") != "Device") {
                    std::string capacity = ReadLine(supply_dir + "/capacity");
                    if (!capacity.empty()) { battery_percent = static_cast<u32>(std::atoi(capacity.c_str())); }
                    battery_status = ReadLine(supply_dir + "/status");
                }
            }
            closedir(supplies);
        }
        // No Mains supply listed: fall back on whether the battery says it is discharging.
        if (ac_power == 255 && !battery_status.empty()) { ac_power = battery_status == "Discharging" ? 0 : 1; }

        // No single "power plan" on Linux: combine the ACPI platform profile (what the desktop's power
        // profile sets, when the firmware exposes one) with cpu0's cpufreq governor and
        // energy-performance preference.
        std::string platform_profile = ReadLine("/sys/firmware/acpi/platform_profile");
        std::string governor = ReadLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
        std::string epp = ReadLine("/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference");
        std::string cpufreq_details;
        if (!governor.empty()) { cpufreq_details += "governor=" + governor; }
        if (!epp.empty()) { cpufreq_details += (cpufreq_details.empty() ? "epp=" : ", epp=") + epp; }
        std::string power_plan = "unknown";
        if (!platform_profile.empty()) {
            power_plan = cpufreq_details.empty() ? platform_profile : platform_profile + " (" + cpufreq_details + ")";
        } else if (!cpufreq_details.empty()) {
            power_plan = cpufreq_details;
        }

        json.Field("hostname", name.nodename);
        json.Field("os", std::string(name.sysname) + " " + name.release);
        json.Field("cpu", cpu_name);
        json.Field("cpu_base_mhz", cpu_base_mhz);
        json.Field("cpu_base_mhz_source", cpu_base_mhz_source);
        json.Field("cpu_physical_cores", static_cast<u32>(cores.size()));
        json.Field("cpu_l3_mb", static_cast<f64>(l3_bytes) / (1024.0 * 1024.0));
        json.Field("ram_total_mb", ram_bytes / (1024 * 1024));
        json.Field("ac_power", ac_power);
        json.Field("battery_percent", battery_percent);
        json.Field("power_plan", power_plan);
    }
#endif

    void WriteGpuFields(JsonWriter& json) {
        VkPhysicalDeviceDriverProperties driver_properties = {};
        driver_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        VkPhysicalDeviceProperties2 properties = {};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &driver_properties;
        vkGetPhysicalDeviceProperties2(VkVault::PhysicalDevice, &properties);
        const VkPhysicalDeviceProperties& props = properties.properties;

        // The heightmap array's real cap is per format, not just limits.maxImageArrayLayers.
        VkImageFormatProperties heightmap_format = {};
        vkGetPhysicalDeviceImageFormatProperties(
            VkVault::PhysicalDevice, VK_FORMAT_R16_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0, &heightmap_format
        );

        VkPhysicalDeviceMemoryProperties memory_properties;
        vkGetPhysicalDeviceMemoryProperties(VkVault::PhysicalDevice, &memory_properties);
        u64 device_local_bytes = 0;
        for (u32 i = 0; i < memory_properties.memoryHeapCount; i++) {
            if (memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                device_local_bytes += memory_properties.memoryHeaps[i].size;
            }
        }

        json.Field("gpu", props.deviceName);
        json.Field("gpu_type", DeviceTypeName(props.deviceType));
        json.Field("gpu_vendor_id", props.vendorID);
        json.Field("gpu_device_id", props.deviceID);
        json.Field("vulkan_api", VersionString(props.apiVersion));
        json.Field("driver_name", driver_properties.driverName);
        json.Field("driver_info", driver_properties.driverInfo);
        json.Field("driver_version_raw", props.driverVersion);
        json.Field("gpu_device_local_mb", device_local_bytes / (1024 * 1024));
        json.Field("max_image_array_layers", props.limits.maxImageArrayLayers);
        json.Field("r16_unorm_max_array_layers", heightmap_format.maxArrayLayers);
        json.Field("timestamp_period_ns", props.limits.timestampPeriod);

        json.Key("memory_heaps");
        json.BeginArray();
        for (u32 i = 0; i < memory_properties.memoryHeapCount; i++) {
            json.BeginObject();
            json.Field("size_mb", memory_properties.memoryHeaps[i].size / (1024 * 1024));
            json.Field("device_local", (memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0);
            json.EndObject();
        }
        json.EndArray();

        json.Field("present_mode", PresentModeName(VkVault::PresentMode));
    }

    void WriteDisplayFields(JsonWriter& json) {
        SDL_DisplayID display = Window::SdlWindow ? SDL_GetDisplayForWindow(Window::SdlWindow) : SDL_GetPrimaryDisplay();
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
        if (!mode) { return; }

        json.Field("display_width", static_cast<i32>(mode->w));
        json.Field("display_height", static_cast<i32>(mode->h));
        json.Field("display_refresh_hz", static_cast<f64>(mode->refresh_rate));
    }

    void WriteMachineFields(JsonWriter& json) {
        json.Field("logical_threads", std::thread::hardware_concurrency());
        WriteOsFields(json);
        WriteGpuFields(json);
        WriteDisplayFields(json);
    }
}

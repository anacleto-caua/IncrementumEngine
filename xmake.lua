-- For dev run:     xmake run
-- For profiling:   xmake f -m releasedbg && xmake

-- Project Settings
add_rules("mode.debug", "mode.release", "mode.releasedbg")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "./build/"})

set_project("Incrementum Engine - The terrain thing.")
set_version("0.0.1")

-- Defaults to using clang and debug build
set_languages("c++23")
set_defaultmode("debug")

set_toolchains("clang")

-- Custom rule for shader compilation
rule("compile_shaders")
    set_extensions(".vert", ".frag", ".comp")
    on_build_file(function (target, sourcefile, opt)
        import("core.project.depend")
        import("lib.detect.find_program")
        local shader_name = path.filename(sourcefile)
        local output_file = path.join(target:targetdir(), "shaders", shader_name .. ".spv")

        local glslc = nil
        local vk_sdk = os.getenv("VULKAN_SDK")

        if vk_sdk then
            local ext = is_plat("windows") and ".exe" or ""
            glslc = path.join(vk_sdk, "bin", "glslc" .. ext)
        end

        if not glslc or not os.isfile(glslc) then
            glslc = find_program("glslc")
        end

        if glslc then
            depend.on_changed(function ()
                local outdir = path.directory(output_file)
                if not os.exists(outdir) then
                    os.mkdir(outdir)
                end
                if not os.exists(output_file) then
                    os.touch(output_file)
                end

                os.vrunv(glslc, {sourcefile, "-o", output_file})
                print("Compiling: " .. shader_name .. " -> " .. output_file)
            end, {files = sourcefile})
        else
            print("Warning: glslc not found. Skipping: " .. shader_name)
        end
    end)

-- Custom rule for coping assets
rule("copy_assets")
    on_build_file(function (target, sourcefile, opt)
        import("core.project.depend")
        local rel_path = path.relative(sourcefile, "resources")
        local dest_file = path.join(target:targetdir(), "resources", rel_path)
        depend.on_changed(function()
            os.cp(sourcefile, dest_file)
            print("Copying asset: " .. path.filename(sourcefile) .. " -> " .. dest_file)
        end, {files = sourcefile})
    end)

target("IncrementumEngine")
    set_kind("binary")
    set_default()

    -- Generate debug files, keep symbols and disable optimazations
    if is_mode("debug") then
        set_symbols("debug")
        set_strip("none")
        set_optimize("none")

    -- Release Debug version for profiling
    elseif is_mode("releasedbg") then
        set_symbols("debug")
        set_optimize("fastest")
        set_strip("none")

        -- Crucial for AMD μProf to unwind Clang call stacks accurately
        add_cxflags("-fno-omit-frame-pointer", {force = true})
    end

    set_warnings("all", "extra")
    add_cxflags("-Wpedantic")
    add_cxflags("-Wshadow")

    -- Pre compiled headers
    set_pcxxheader("src/Utils/PreCompiledHeaders/root.hpp")

    -- Treat third-party libs as system headers to suppress their warnings
    add_sysincludedirs("libs", "libs/vma", "libs/glm-1.0.2", "libs/fnl", "libs/imgui", "libs/imgui/backends")

    -- Add source files
    add_files("src/**.cpp")
    add_includedirs("src")

    -- Include directories and set defines
    add_includedirs("src", "libs", "libs/vma", "libs/glm-1.0.2", "libs/fnl", "libs/imgui", "libs/imgui/backends")

    add_files("libs/imgui/*.cpp")
    add_files("libs/imgui/backends/**.cpp")

    -- Global definitions
    add_defines(
        "GLM_FORCE_RADIANS",
        "GLM_FORCE_LEFT_HANDED",
        "GLM_FORCE_DEPTH_ZERO_TO_ONE",
        "GLM_ENABLE_EXPERIMENTAL",
        "GLFW_INCLUDE_VULKAN"
    )

    if is_plat("windows") then
        -- VULKAN (Env Var)
        local vk_sdk = os.getenv("VULKAN_SDK")
        if vk_sdk then
            add_sysincludedirs(path.join(vk_sdk, "Include"))
            add_linkdirs(path.join(vk_sdk, "Lib"))
        end
        add_syslinks("vulkan-1")

        -- Find GLFW based on the GLFW_ROOT PATH variable - Author's Note: Install it alongside clang using Scoop
        local glfw_root = os.getenv("GLFW_ROOT")
        if not glfw_root or not os.isdir(glfw_root) then
            utils.error("Error: GLFW_ROOT environment variable is not set! Please point it to your GLFW folder.")
        end

        -- Add Include
        add_includedirs(path.join(glfw_root, "include"))

        -- Add Lib
        local lib_dir = path.join(glfw_root, "lib-vc2022")

        -- Fallback for older versions
        if not os.isdir(lib_dir) then lib_dir = path.join(glfw_root, "lib-vc2019") end
        if not os.isdir(lib_dir) then lib_dir = path.join(glfw_root, "lib-vc2017") end

        if os.isdir(lib_dir) then
            add_linkdirs(lib_dir)
            print("Linking GLFW from: " .. lib_dir)
        else
            utils.error("Error: Could not find a compatible 'lib-vcXXXX' folder in " .. glfw_root)
        end

        add_syslinks("glfw3")
        add_syslinks("user32", "gdi32", "shell32")

    elseif is_plat("linux") then
        add_syslinks("vulkan", "glfw")
        add_syslinks("dl", "pthread", "X11", "Xxf86vm", "Xrandr", "Xi")
    end

    -- Build Output Directory
    set_targetdir("build/$(plat)/$(mode)")

    -- Add shader and asset files to trigger the custom rules
    -- At the end, to avoid glslc's "No such file or directory"
    add_files("shaders/**.vert", "shaders/**.frag", "shaders/**.comp", {rule = "compile_shaders"})
    add_files("resources/**", {rule = "copy_assets"})

target_end()

-- Task to kick start rad debugger linked to project binary
task("rad")
    set_menu({
        usage = "xmake rad",
        description = "Builds the project and opens the RAD Debugger."
    })

    on_run(function ()
        import("core.project.config")
        import("core.project.project")

        config.load()
        os.exec("xmake")

        -- Find the binary target file
        local target_file = nil
        for name, target in pairs(project.targets()) do
            if target:kind() == "binary" then
                target_file = target:targetfile()
                break
            end
        end

        -- Launch
        if target_file then
            local native_path = path.translate(target_file)

            os.runv("raddbg.exe", {native_path}, {detach = true})
        else
            print("Error: Could not find a binary target.")
        end
    end)
task_end()

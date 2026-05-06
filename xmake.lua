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
rule_end()

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
rule_end()

package("sdl3")
    set_sourcedir("libs/to_compile/sdl3")
    add_deps("cmake")

    on_load(function (package)
        if package:is_plat("windows") then
            local lib_name = package:debug() and "SDL3-staticd" or "SDL3-static"
            package:add("links", lib_name)
            package:add("syslinks", "user32", "gdi32", "winmm", "imm32", "ole32", "oleaut32", "version", "uuid", "advapi32", "setupapi", "shell32")
        elseif package:is_plat("linux") then
            package:add("links", "SDL3")
            package:add("syslinks", "pthread", "dl", "m")
        end
    end)

    on_install(function (package)
        import("package.tools.cmake")

        local configs = {
            "-DSDL_SHARED=OFF",
            "-DSDL_STATIC=ON",
            "-DSDL_TEST_LIBRARY=OFF"
        }

        cmake.install(package, configs)
    end)
package_end()

add_requires("sdl3")

target("IncrementumEngine")
    set_kind("binary")
    set_default()

    add_packages("sdl3")

    -- Warnings
    set_warnings("all", "extra")
    add_cxflags(
        "-Wpedantic",
        "-Wshadow",
        "-Wconversion",
        "-Wsign-conversion",
        "-Wformat=2"
    )

    -- Generate debug files, keep symbols and disable optimazations
    if is_mode("debug") then
        set_symbols("debug")
        set_strip("none")
        set_optimize("none")

        -- Buffer overflow protection
        add_cxflags("-fstack-protector-strong")
        -- Accurate call stacks when a sanitizer crashes
        add_cxflags("-fno-omit-frame-pointer", {force = true})

        if is_plat("windows") then
            -- Microsoft is a pain with clang's asan, so leave it be.
            add_defines("_ITERATOR_DEBUG_LEVEL=2")
        elseif is_plat("linux") then
            -- Enable's asan
            set_policy("build.sanitizer.address", true)
            set_policy("build.sanitizer.undefined", true)
            add_defines("_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG")
        end

    -- Release Debug version for profiling
    elseif is_mode("releasedbg") then
        set_symbols("debug")
        set_optimize("fastest")
        set_strip("none")

        -- Crucial for AMD μProf to unwind Clang call stacks accurately
        add_cxflags("-fno-omit-frame-pointer", {force = true})
    end

    -- Pre compiled headers
    set_pcxxheader("src/Utils/PreCompiledHeaders/root.hpp")

    -- All libraries
    local lib_includes = {
        "libs",
        "libs/vma",
        "libs/glm-1.0.2",
        "libs/fnl",
        "libs/imgui",
        "libs/imgui/backends",
        "libs/moodycamel"
    }

    -- Treat third-party libs as system headers to suppress their warnings
    add_sysincludedirs(lib_includes)

    -- Add source files
    add_files("src/**.cpp")
    add_includedirs("src")

    -- Include directories and set defines
    add_includedirs(lib_includes)

    add_files("libs/imgui/*.cpp")
    add_files("libs/imgui/backends/**.cpp")

    -- Global definitions
    add_defines(
        "GLM_FORCE_RADIANS",
        "GLM_FORCE_LEFT_HANDED",
        "GLM_FORCE_DEPTH_ZERO_TO_ONE",
        "GLM_ENABLE_EXPERIMENTAL"
    )

    if is_plat("windows") then
        -- VULKAN (Env Var)
        local vk_sdk = os.getenv("VULKAN_SDK")
        if vk_sdk then
            add_sysincludedirs(path.join(vk_sdk, "Include"))
            add_linkdirs(path.join(vk_sdk, "Lib"))
        end

        add_syslinks("vulkan-1")

        add_syslinks("user32", "gdi32", "shell32")

    elseif is_plat("linux") then
        add_syslinks("vulkan")
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

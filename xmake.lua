-- Purple Map Fix - SKSE plugin build file
-- Build with build.bat (fetches CommonLibSSE-NG into lib/ first), then just: xmake

set_xmakever("2.8.2")

set_project("PurpleMapFix")
set_version("1.1.0")
set_license("MIT")

set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")

-- Build for Skyrim SE and AE (one DLL works on both). VR uses a different
-- address library, so it's turned off.
set_config("skyrim_se", true)
set_config("skyrim_ae", true)
set_config("skyrim_vr", false)

includes("lib/commonlibsse-ng")

-- zlib reads compressed records in plugin files
add_requires("zlib")

target("PurpleMapFix")
    add_deps("commonlibsse-ng")
    add_packages("zlib")

    add_rules("commonlibsse-ng.plugin", {
        name = "PurpleMapFix",
        author = "RahmelJackson",
        description = "Keeps the world map readable: restores map settings reset by load order and patches missing map textures",
        options = {
            address_library = true,
            signature_scanning = false,
            struct_dependent = false
        }
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    -- Copy the built DLL into dist/ so the mod folder is ready to zip.
    after_build(function (target)
        local out = path.join(os.projectdir(), "dist", "SKSE", "Plugins")
        os.mkdir(out)
        os.cp(target:targetfile(), out)
        local pdb = target:symbolfile()
        if pdb and os.isfile(pdb) then
            os.cp(pdb, out)
        end
        print("Copied plugin to " .. out)
    end)

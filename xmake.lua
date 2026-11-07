set_xmakever("3.0.0")

set_project("DirectionalCombat")
set_version("0.1.0")
set_license("MIT")

set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.releasedbg", "mode.debug")

-- Wszystkie runtime'y włączone, bo tak jest zbudowana gotowa (prebuilt) CommonLibSSE-NG.
-- Wtyczka sama wyłącza się w Skyrim VR (patrz main.cpp).
set_config("skyrim_se", true)
set_config("skyrim_ae", true)
set_config("skyrim_vr", true)

includes("lib/commonlibsse-ng")

target("DirectionalCombat")
    add_rules("commonlibsse-ng.plugin", {
        name = "DirectionalCombat",
        author = "pablo",
        description = "Analog / directional melee combat (Dead Island / Mordhau style)"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/PCH.h")

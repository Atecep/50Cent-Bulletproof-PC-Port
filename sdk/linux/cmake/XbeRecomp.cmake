# Original Xbox static recompilation: xbe_recomp, the host runtime and add_xbe_image()
include_guard(GLOBAL)

get_filename_component(XBE_SDK_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
# build scripts find the SDK's xdk folder and THIRD_PARTY.txt through this
file(TO_NATIVE_PATH "${XBE_SDK_DIR}" XBE_SDK_DIR_NATIVE)
file(WRITE ${CMAKE_BINARY_DIR}/sdk_dir.txt "${XBE_SDK_DIR_NATIVE}\n")
file(TO_NATIVE_PATH "${XBE_XDK_DIR}" XBE_XDK_DIR_NATIVE)
file(WRITE ${CMAKE_BINARY_DIR}/xdk_dir.txt "${XBE_XDK_DIR_NATIVE}\n")


# clang targeting the MSVC ABI leaves CMAKE_<LANG>_FLAGS_<CONFIG> empty here, so a Release build
# compiled the host runtime, the dependencies and the tools at -O0 while only the generated guest
# code carried the -O2 set per source below. Measured consequence: the recorder's per-draw state
# compare cost several times what its byte count predicts. NDEBUG is deliberately left undefined -
# the log's verbose mode keys off it (core/log.cpp), and dropping to the release log format is a
# separate decision from optimising.
foreach(cfg RELEASE RELWITHDEBINFO)
    if(NOT CMAKE_C_FLAGS_${cfg} MATCHES "[-/]O")
        set(CMAKE_C_FLAGS_${cfg} "${CMAKE_C_FLAGS_${cfg}} -O2" CACHE STRING "" FORCE)
    endif()
    if(NOT CMAKE_CXX_FLAGS_${cfg} MATCHES "[-/]O")
        set(CMAKE_CXX_FLAGS_${cfg} "${CMAKE_CXX_FLAGS_${cfg}} -O2" CACHE STRING "" FORCE)
    endif()
endforeach()
if(NOT FETCHCONTENT_SOURCE_DIR_ZYDIS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/out/deps-src/zydis-4.1.1")
    set(FETCHCONTENT_SOURCE_DIR_ZYDIS "${CMAKE_CURRENT_SOURCE_DIR}/out/deps-src/zydis-4.1.1")
endif()
if(NOT FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/out/deps-src/tomlplusplus-3.4.0")
    set(FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS "${CMAKE_CURRENT_SOURCE_DIR}/out/deps-src/tomlplusplus-3.4.0")
endif()
if(NOT FETCHCONTENT_SOURCE_DIR_IMGUI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/out/deps-src/imgui-1.91.9b")
    set(FETCHCONTENT_SOURCE_DIR_IMGUI "${CMAKE_CURRENT_SOURCE_DIR}/out/deps-src/imgui-1.91.9b")
endif()
if(NOT FETCHCONTENT_SOURCE_DIR_SDL3 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/out/deps-src/SDL-release-3.4.16")
    set(FETCHCONTENT_SOURCE_DIR_SDL3 "${CMAKE_CURRENT_SOURCE_DIR}/out/deps-src/SDL-release-3.4.16")
endif()

include(FetchContent)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zydis GIT_REPOSITORY https://github.com/zyantific/zydis.git GIT_TAG v4.1.1 GIT_SHALLOW TRUE)
FetchContent_Declare(tomlplusplus GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git GIT_TAG v3.4.0 GIT_SHALLOW TRUE)
# v1.91.9b, not v1.91.0: the SDL3 platform backend in 1.91.0 predates SDL3's release and no longer
# compiles against SDL3 3.4 (SDL_SetRelativeMouseMode / SDL_CaptureMouse signatures). Measured on this
# box: 1.91.0 imgui_impl_sdl3.cpp = 2 errors, 1.91.9b = 0; imgui_impl_vulkan and the win32/dx12 pair
# compile clean on both, so the Windows build is unaffected.
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9b GIT_SHALLOW TRUE)
# SDL3 for controllers, static so it links into the exe
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
option(XBE_LINUX_PORTABLE_EXPERIMENT "Build an ELF guest-loading prototype alongside the static executable" OFF)
option(XBE_WINDOWS_PORTABLE "Build the Windows x64 portable distribution host" OFF)
if(XBE_WINDOWS_PORTABLE AND (NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
        NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"))
    message(FATAL_ERROR "XBE_WINDOWS_PORTABLE requires Windows x64 and Clang targeting the MSVC ABI")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND XBE_LINUX_PORTABLE_EXPERIMENT)
    # Wayland needs SDL's texture-backed window surface for the preparation UI.
    set(SDL_RENDER ON CACHE BOOL "" FORCE)
    set(SDL_RENDER_VULKAN OFF CACHE BOOL "" FORCE)
else()
    set(SDL_RENDER OFF CACHE BOOL "" FORCE)
endif()
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.4.16 GIT_SHALLOW TRUE)
if(CMAKE_CROSSCOMPILING)
    # Zydis is only for the tools, which a cross build takes from the build machine (below)
    FetchContent_MakeAvailable(tomlplusplus imgui SDL3)
else()
    FetchContent_MakeAvailable(zydis tomlplusplus imgui SDL3)
endif()

if(CMAKE_CROSSCOMPILING)
# xbe_recomp runs during the build, so a cross build takes it from a build of cmake/XbeTools.cmake
# for this (the build) machine instead of building it for the target. xbe_symbols is only run by hand.
set(XBE_HOST_TOOLS_DIR "" CACHE PATH "build directory of cmake/XbeTools.cmake for the build machine (xbe_recomp)")
set(XBE_RECOMP_HOST ${XBE_HOST_TOOLS_DIR}/xbe_recomp${CMAKE_HOST_EXECUTABLE_SUFFIX})
if(NOT XBE_HOST_TOOLS_DIR OR NOT EXISTS ${XBE_RECOMP_HOST})
    message(FATAL_ERROR "cross-compiling runs xbe_recomp on this machine, and XBE_HOST_TOOLS_DIR=\"${XBE_HOST_TOOLS_DIR}\" "
        "holds no xbe_recomp${CMAKE_HOST_EXECUTABLE_SUFFIX}: build cmake/XbeTools.cmake for this machine first "
        "and point XBE_HOST_TOOLS_DIR at that build directory")
endif()
add_executable(xbe_recomp IMPORTED)
set_target_properties(xbe_recomp PROPERTIES IMPORTED_LOCATION ${XBE_RECOMP_HOST})
else()
# x86 to C++ recompiler
file(GLOB XBE_RECOMP_SOURCES CONFIGURE_DEPENDS ${XBE_SDK_DIR}/tools/recomp/*.cpp)
add_executable(xbe_recomp ${XBE_RECOMP_SOURCES})
target_include_directories(xbe_recomp PRIVATE ${XBE_SDK_DIR}/runtime ${XBE_SDK_DIR}/tools/recomp)
target_compile_definitions(xbe_recomp PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(xbe_recomp PRIVATE Zydis tomlplusplus::tomlplusplus)

# XDK signature scanner that writes a new title's config
add_executable(xbe_symbols ${XBE_SDK_DIR}/tools/symbols/main.cpp)
target_include_directories(xbe_symbols PRIVATE ${XBE_SDK_DIR}/runtime)
target_compile_definitions(xbe_symbols PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(xbe_symbols PRIVATE Zydis tomlplusplus::tomlplusplus)
endif()

set(XBE_IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)

file(GLOB XBE_RUNTIME_SOURCES CONFIGURE_DEPENDS ${XBE_SDK_DIR}/runtime/*/*.cpp)
if(WIN32)
    # On Windows, splash.cpp uses WIC, so image_decode.cpp is not needed
    list(REMOVE_ITEM XBE_RUNTIME_SOURCES ${XBE_SDK_DIR}/runtime/app/image_decode.cpp)
else()
    find_package(ZLIB REQUIRED)  # runtime/app/image_decode.cpp: the splash PNG decoder
endif()
find_package(Vulkan QUIET)
set(XBE_LOCAL_VULKAN "${CMAKE_CURRENT_SOURCE_DIR}/out/deps-src/vulkan")
if(WIN32 AND NOT Vulkan_FOUND AND EXISTS "${XBE_LOCAL_VULKAN}/include/vulkan/vulkan.h"
   AND EXISTS "${XBE_LOCAL_VULKAN}/lib/vulkan-1.lib")
    if(NOT Vulkan_INCLUDE_DIR)
        set(Vulkan_INCLUDE_DIR "${XBE_LOCAL_VULKAN}/include")
    endif()
    if(NOT Vulkan_LIBRARY)
        set(Vulkan_LIBRARY "${XBE_LOCAL_VULKAN}/lib/vulkan-1.lib")
    endif()
endif()
if(NOT Vulkan_FOUND)
    find_package(Vulkan REQUIRED)
endif()
# Shader compilers for the Vulkan backend. runtime/gpu/spirv.cpp carries both paths and picks at run
# time: DXC is dlopen'd / LoadLibrary'd, so it is never linked - it only needs XBE_HAVE_DXC and its
# headers; glslang is linked and enabled only when CMake finds its libraries. Both are
# offered when found. XBE_SHADER_BACKEND makes one of them required instead of optional.
set(XBE_SHADER_BACKEND "" CACHE STRING "require a Vulkan shader compiler: dxc or glslang (empty: whatever is found)")
set_property(CACHE XBE_SHADER_BACKEND PROPERTY STRINGS dxc glslang)
set(XBE_DXC_ROOT "" CACHE PATH "Root of the DXC SDK")
if(WIN32)
    find_path(XBE_DXC_INCLUDE NAMES dxcapi.h dxc/dxcapi.h
        HINTS "${XBE_DXC_ROOT}" ENV DXC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/out/deps-src/dxc"
        PATH_SUFFIXES inc include)
    if(XBE_DXC_INCLUDE AND NOT EXISTS "${XBE_DXC_INCLUDE}/dxcapi.h" AND NOT EXISTS "${XBE_DXC_INCLUDE}/dxc/dxcapi.h")
        set(XBE_DXC_INCLUDE "")
    endif()
    if(XBE_DXC_INCLUDE)
        find_file(XBE_DXC_RUNTIME NAMES dxcompiler.dll
            HINTS "${XBE_DXC_ROOT}" ENV DXC_ROOT "${XBE_DXC_INCLUDE}/.."
            PATH_SUFFIXES bin/x64 bin)
        if(EXISTS "${XBE_DXC_RUNTIME}")
            get_filename_component(XBE_DXC_BIN "${XBE_DXC_RUNTIME}" DIRECTORY)
        else()
            set(XBE_DXC_INCLUDE "")
        endif()
    endif()
else()
    find_path(XBE_DXC_INCLUDE NAMES dxc/WinAdapter.h WinAdapter.h HINTS "${XBE_DXC_ROOT}" ENV DXC_ROOT /opt/dxc
        PATH_SUFFIXES include)
    if(NOT (EXISTS "${XBE_DXC_INCLUDE}/dxc/dxcapi.h" AND EXISTS "${XBE_DXC_INCLUDE}/dxc/WinAdapter.h")
       AND NOT (EXISTS "${XBE_DXC_INCLUDE}/dxcapi.h" AND EXISTS "${XBE_DXC_INCLUDE}/WinAdapter.h"))
        set(XBE_DXC_INCLUDE "")
    endif()
endif()
if(NOT XBE_SHADER_BACKEND STREQUAL "dxc")
    find_package(glslang CONFIG QUIET)
    if(NOT TARGET glslang::glslang)
        find_library(XBE_GLSLANG glslang)
        find_library(XBE_SPIRV SPIRV)
        find_library(XBE_GLSLANG_RES glslang-default-resource-limits)
        find_path(XBE_GLSLANG_INCLUDE glslang/Public/ShaderLang.h)
    endif()
endif()
set(XBE_SHADER_LIBS "")
set(XBE_SHADER_DEFINES "")
set(XBE_SHADER_INCLUDES "")
if(XBE_DXC_INCLUDE AND NOT XBE_SHADER_BACKEND STREQUAL "glslang")
    list(APPEND XBE_SHADER_DEFINES XBE_HAVE_DXC=1)
    list(APPEND XBE_SHADER_INCLUDES ${XBE_DXC_INCLUDE})
    set(XBE_HAVE_DXC_FOUND TRUE)
    if(NOT WIN32)
        find_library(XBE_DXC_RUNTIME dxcompiler
            HINTS "${XBE_DXC_ROOT}" ENV DXC_ROOT "${XBE_DXC_INCLUDE}/.." /opt/dxc
            PATH_SUFFIXES lib lib64)
        if(XBE_DXC_RUNTIME)
            list(APPEND XBE_SHADER_DEFINES "XBE_DXC_LIBRARY_PATH=\"${XBE_DXC_RUNTIME}\"")
        endif()
    endif()
endif()
if(TARGET glslang::glslang AND TARGET glslang::SPIRV AND TARGET glslang::glslang-default-resource-limits)
    list(APPEND XBE_SHADER_LIBS glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits)
    set(XBE_HAVE_GLSLANG_FOUND TRUE)
elseif(NOT XBE_SHADER_BACKEND STREQUAL "dxc" AND XBE_GLSLANG AND XBE_SPIRV AND XBE_GLSLANG_RES AND XBE_GLSLANG_INCLUDE)
    list(APPEND XBE_SHADER_LIBS ${XBE_GLSLANG} ${XBE_SPIRV} ${XBE_GLSLANG_RES})
    list(APPEND XBE_SHADER_INCLUDES ${XBE_GLSLANG_INCLUDE})
    set(XBE_HAVE_GLSLANG_FOUND TRUE)
endif()
if(XBE_HAVE_GLSLANG_FOUND)
    list(APPEND XBE_SHADER_DEFINES XBE_HAVE_GLSLANG=1)
endif()
if(XBE_SHADER_BACKEND STREQUAL "dxc" AND NOT XBE_HAVE_DXC_FOUND)
    message(FATAL_ERROR "DXC unavailable: set XBE_DXC_ROOT to a compatible SDK (headers and, on Windows, x64 dxcompiler.dll)")
elseif(XBE_SHADER_BACKEND STREQUAL "glslang" AND NOT XBE_HAVE_GLSLANG_FOUND)
    message(FATAL_ERROR "XBE_SHADER_BACKEND=glslang but glslang/SPIRV/glslang-default-resource-limits were not all found")
elseif(NOT XBE_HAVE_DXC_FOUND AND NOT XBE_HAVE_GLSLANG_FOUND)
    message(FATAL_ERROR "no HLSL to SPIR-V compiler: install DXC or glslang")
endif()
message(STATUS "Vulkan shader compilers: DXC=${XBE_HAVE_DXC_FOUND} glslang=${XBE_HAVE_GLSLANG_FOUND}")
# SDL's headers go to the input code alone (it is linked LINK_ONLY), so the generated sources' compile line stays the same
set_source_files_properties(${XBE_SDK_DIR}/runtime/core/input.cpp PROPERTIES INCLUDE_DIRECTORIES "${sdl3_SOURCE_DIR}/include")

function(xbe_write_image_hash target sha1 output)
    set(source "${CMAKE_CURRENT_BINARY_DIR}/${target}_xbe.cpp")
    file(CONFIGURE OUTPUT "${source}" CONTENT "extern const char kImageSha1[] = \"${sha1}\";\n")
    set(${output} "${source}" PARENT_SCOPE)
endfunction()

function(xbe_configure_host target)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XBE_SDK_DIR}/runtime")
    target_include_directories(${target} SYSTEM PRIVATE ${imgui_SOURCE_DIR} ${XBE_SHADER_INCLUDES})
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fno-strict-aliasing;-msse4.1>")
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS XBE_VK_TARGET=1
        ${XBE_SHADER_DEFINES})
    target_link_libraries(${target} PRIVATE tomlplusplus::tomlplusplus SDL3::SDL3-static Vulkan::Vulkan
        ${XBE_SHADER_LIBS})
    if(WIN32)
        target_link_libraries(${target} PRIVATE ole32 windowscodecs dbghelp winmm dwmapi ws2_32)
    else()
        target_link_libraries(${target} PRIVATE ZLIB::ZLIB pthread dl)
    endif()
endfunction()

function(xbe_stage_dxc target)
    if(WIN32 AND XBE_HAVE_DXC_FOUND)
        set(dxc_runtime "${XBE_DXC_RUNTIME}")
        if(EXISTS "${XBE_DXC_BIN}/dxil.dll")
            list(APPEND dxc_runtime "${XBE_DXC_BIN}/dxil.dll")
        endif()
        add_custom_target(${target}_dxc_runtime
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${dxc_runtime} "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
        add_dependencies(${target} ${target}_dxc_runtime)
    endif()
endfunction()

# One executable per recompiled image: its code from config/<config> (partitions as that config sets them),
# the SDK runtime, XBE_HOST_SOURCES shared by every image of the game, and src/<game_dir> hooks only this image has.
# Expects assets/<xbe> and the config's out = "generated/<game_dir>". At run time the game may be a disc image instead.
function(add_xbe_image target xbe config partitions game_dir)
    set(GEN ${CMAKE_CURRENT_BINARY_DIR}/generated/${game_dir})
    set(GEN_SOURCES ${GEN}/x86_func_mapping.cpp)
    math(EXPR last "${partitions} - 1")
    foreach(i RANGE ${last})
        list(APPEND GEN_SOURCES ${GEN}/x86_recomp.${i}.cpp)
    endforeach()
    file(GLOB configs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/config/*.toml ${XBE_XDK_DIR}/*/*.toml)
    add_custom_command(
        OUTPUT ${GEN}/codegen.stamp
        BYPRODUCTS ${GEN_SOURCES} ${GEN}/x86_recomp_shared.h ${GEN}/image_symbols.h
        COMMAND xbe_recomp config/${config} "${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe}" "${GEN}" --xdk ${XBE_XDK_DIR}
        COMMAND ${CMAKE_COMMAND} -E touch ${GEN}/codegen.stamp
        DEPENDS xbe_recomp ${configs} ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Recompiling ${xbe}"
        VERBATIM)
    add_custom_target(recompile_${target} DEPENDS ${GEN}/codegen.stamp)
    set_source_files_properties(${GEN_SOURCES} PROPERTIES GENERATED TRUE
        COMPILE_OPTIONS "-O2;-fno-strict-aliasing;-Wno-unused-label;-Wno-parentheses-equality;-Wno-unused-variable")
    file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${game_dir}/*.cpp)
    # the xbe's SHA-1: the executable checks the game it finds is the release it was made from
    set(xbe_file ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${xbe_file})
    file(SHA1 ${xbe_file} xbe_sha1)
    xbe_write_image_hash(${target} "${xbe_sha1}" hash_source)
    add_executable(${target} ${XBE_RUNTIME_SOURCES} ${XBE_HOST_SOURCES} ${GAME_SOURCES} ${GEN_SOURCES}
        ${XBE_IMGUI_SOURCES} ${hash_source})
    if(WIN32)
        set_property(TARGET ${target} PROPERTY WIN32_EXECUTABLE "$<CONFIG:Release>")
        target_compile_definitions(${target} PRIVATE "$<$<CONFIG:Release>:XBE_WINDOWS_GUI>")
    endif()
    add_dependencies(${target} recompile_${target})
    xbe_configure_host(${target})
    target_include_directories(${target} PRIVATE "${GEN}")
    xbe_stage_dxc(${target})
    # lld keeps the recompiled weak functions overridable by the host ones; the debug link writes the
    # PDB that names them in a Windows crash dump
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        if(NOT CMAKE_LINKER MATCHES "lld-link")
            set(CMAKE_LINKER "lld-link" CACHE FILEPATH "" FORCE)
        endif()
        target_link_options(${target} PRIVATE /DEBUG)
    else()
        target_link_options(${target} PRIVATE -fuse-ld=lld -g)
    endif()
endfunction()

# The portable host consumes image metadata. It has no dependency on a disc or translated guest objects.
function(add_xbe_portable portable config sha1 game_dir)
    if(NOT EXISTS "${config}")
        message(FATAL_ERROR "Portable host metadata not found: ${config}. Set BULLETPROOF_PORTABLE_CONFIG to the verified title TOML.")
    endif()
    set(host ${portable}_host)
    set(bridge ${CMAKE_CURRENT_BINARY_DIR}/${portable}_bridge.cpp)
    set(objects ${CMAKE_CURRENT_BINARY_DIR}/${portable}_objects.txt)
    xbe_write_image_hash(${portable} "${sha1}" hash_source)
    file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${game_dir}/*.cpp)
    add_library(${host} OBJECT EXCLUDE_FROM_ALL ${XBE_RUNTIME_SOURCES} ${XBE_HOST_SOURCES} ${GAME_SOURCES}
        ${XBE_IMGUI_SOURCES} ${hash_source} ${XBE_SDK_DIR}/runtime/app/portable_guest.cc)
    set(metadata ${CMAKE_CURRENT_BINARY_DIR}/${portable}_metadata)
    file(GLOB layout_files CONFIGURE_DEPENDS "${XBE_XDK_DIR}/*/layout.toml")
    add_custom_command(OUTPUT ${metadata}/image_symbols.h
        COMMAND xbe_recomp "${config}"
            --xdk "${XBE_XDK_DIR}" --image-symbols-only "${metadata}"
        DEPENDS xbe_recomp "${config}" ${layout_files}
        VERBATIM)
    target_sources(${host} PRIVATE ${metadata}/image_symbols.h)
    xbe_configure_host(${host})
    target_include_directories(${host} BEFORE PRIVATE ${metadata})
    target_include_directories(${host} PRIVATE "${sdl3_SOURCE_DIR}/src/render")
    target_compile_definitions(${host} PRIVATE XBE_PORTABLE_GUEST=1)
    file(GENERATE OUTPUT ${objects} CONTENT "$<JOIN:$<TARGET_OBJECTS:${host}>,\n>\n")
    if(WIN32)
        target_compile_definitions(${host} PRIVATE "$<$<CONFIG:Release>:XBE_WINDOWS_GUI>")
        set(support ${CMAKE_CURRENT_BINARY_DIR}/${portable}_support)
        find_program(guest_nm llvm-nm REQUIRED)
        find_program(guest_lld lld-link REQUIRED)
        set(bridge_args -DWINDOWS_GUEST=ON -DSUPPORT=${support})
        set(bridge_files ${support}/guest_hook_list.inc ${support}/guest_hook_defines.h ${support}/guest.def)
    else()
        set(guest_nm ${CMAKE_NM})
    endif()
    add_custom_command(OUTPUT ${bridge} ${bridge_files}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/${portable}_support
        COMMAND ${CMAKE_COMMAND} -DNM=${guest_nm} -DOBJECT_LIST=${objects} -DOUTPUT=${bridge} ${bridge_args}
            -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GuestBridge.cmake
        DEPENDS ${host} $<TARGET_OBJECTS:${host}> ${objects}
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GuestBridge.cmake
        VERBATIM)
    add_executable(${portable} ${ARGN} ${bridge})
    target_link_libraries(${portable} PRIVATE ${host})
    target_include_directories(${portable} PRIVATE ${XBE_SDK_DIR}/runtime)
    if(WIN32)
        target_sources(${portable} PRIVATE ${bridge_files})
        target_include_directories(${portable} PRIVATE ${support})
        set(guest_imports)
        foreach(name IN ITEMS vcruntime ucrt-heap ucrt-math)
            set(def ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/windows_guest/${name}.def)
            set(archive ${support}/lib/xbe-${name}.lib)
            add_custom_command(OUTPUT ${archive}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${support}/lib
                COMMAND ${guest_lld} /lib /machine:x64 /def:${def} /out:${archive}
                DEPENDS ${def} ${guest_lld}
                VERBATIM)
            list(APPEND guest_imports ${archive})
        endforeach()
        add_custom_target(${portable}_imports DEPENDS ${guest_imports})
        add_dependencies(${portable} ${portable}_imports)
        set_property(TARGET ${portable} PROPERTY WIN32_EXECUTABLE "$<CONFIG:Release>")
        xbe_stage_dxc(${portable})
    else()
        target_link_options(${portable} PRIVATE -fuse-ld=lld)
        set_target_properties(${portable} PROPERTIES ENABLE_EXPORTS ON
            BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN/lib")
    endif()
endfunction()

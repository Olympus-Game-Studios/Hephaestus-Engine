cmake_minimum_required(VERSION 3.24)
project(HephaestusEngine LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Application Details  (single source of truth — edit these, not C++ files)
# ─────────────────────────────────────────────────────────────────────────────
set(HEPHAESTUS_VERSION_MAJOR 2026) # Major = Year of release
set(HEPHAESTUS_VERSION_MINOR 11) # Minor = Month of release
set(HEPHAESTUS_VERSION_PATCH 12) # Patch = Day of release, unpadded (the C++ macro must be a decimal literal)
set(HEPHAESTUS_VERSION "${HEPHAESTUS_VERSION_MAJOR}.${HEPHAESTUS_VERSION_MINOR}.${HEPHAESTUS_VERSION_PATCH}")

# Major versions of the engine are meant to be released by year
# Minor versions are meant to be released by month
# Patch versions are meant to be released by day
# Version number format: Major.Minor.Patch

set(HEPHAESTUS_APP_NAME        "Hephaestus Engine")
set(HEPHAESTUS_APP_DESCRIPTION "A 3D game engine built with C++20, Vulkan, Qt, and Lua.")
set(HEPHAESTUS_ORGANIZATION    "Hephaestus")
set(HEPHAESTUS_DOMAIN          "dev.olympusgames.hephaestus")
set(HEPHAESTUS_WEBSITE         "https://hephaestus.olympusgames.dev")
set(HEPHAESTUS_COPYRIGHT       "Copyright (c) 2026 Andrew Zambazos")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/Core/AppConfig.h.in"
    "${CMAKE_BINARY_DIR}/generated/AppConfig.h"
    @ONLY
)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum OS X deployment version")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")

find_package(Vulkan REQUIRED)

if(APPLE)
    # On macOS, we might need to link against MoltenVK explicitly if not handled by the loader
    # But usually Vulkan::Vulkan is enough if the SDK is set up correctly.
    # We might need to find other libs like ObjC foundation if we do native stuff.
endif()

add_subdirectory(extern/glfw)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/ufbx/ufbx.h")
    message(FATAL_ERROR
        "ufbx is missing at extern/ufbx (required for FBX import). "
        "Run: git submodule update --init --recursive")
endif()
file(GLOB HEPHAESTUS_RECAST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/recastnavigation/Recast/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/recastnavigation/Detour/Source/*.cpp")
add_library(RecastDetour STATIC ${HEPHAESTUS_RECAST_SOURCES})
target_include_directories(RecastDetour PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/recastnavigation/Recast/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/recastnavigation/Detour/Include")

add_library(ufbx STATIC extern/ufbx/ufbx.c)
set_target_properties(ufbx PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_include_directories(ufbx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/extern/ufbx")
target_compile_definitions(ufbx PRIVATE
    UFBX_NO_SUBDIVISION
    UFBX_NO_TESSELLATION
    UFBX_NO_GEOMETRY_CACHE
)

function(hephaestus_target_ufbx target)
    cmake_parse_arguments(FBX "STATIC;SKELETAL" "" "" ${ARGN})
    target_link_libraries(${target} PRIVATE ufbx)
    target_sources(${target} PRIVATE engine/Assets/FbxDocument.cpp)
    if(FBX_STATIC)
        target_sources(${target} PRIVATE engine/Assets/FbxStaticImporter.cpp)
    endif()
    if(FBX_SKELETAL)
        target_sources(${target} PRIVATE engine/Assets/FbxSkeletalImporter.cpp)
    endif()
endfunction()

# Lua scripting support - try to find it, but make it optional for now
# Check for vcpkg installation first
set(VCPKG_ROOT "$ENV{USERPROFILE}/vcpkg" CACHE PATH "Path to vcpkg installation")
if(EXISTS "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" AND NOT CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Found vcpkg at ${VCPKG_ROOT}")
    # Vcpkg is available - it will handle finding Lua
    # This is particularly useful when building from scratch
endif()

# Try to find Lua via standard mechanisms (vcpkg, system paths, etc.)
find_package(Lua QUIET)
if(NOT Lua_FOUND)
    message(STATUS "Lua not found via standard search paths")
    
    # Try bundled Lua in extern/lua (MSVC import lib + headers)
    set(BUNDLED_LUA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/extern/lua")
    if(EXISTS "${BUNDLED_LUA_PATH}/lua.h" AND EXISTS "${BUNDLED_LUA_PATH}/lua54.lib")
        message(STATUS "Found bundled Lua in extern/lua")
        set(LUA_INCLUDE_DIR "${BUNDLED_LUA_PATH}")
        set(LUA_LIBRARIES "${BUNDLED_LUA_PATH}/lua54.lib")
        set(LUA_DLL_PATH "${BUNDLED_LUA_PATH}/lua54.dll")
        set(LUA_FOUND TRUE)
    else()
        # Try manual search in vcpkg directories
        set(VCPKG_TRIPLET "x64-windows")
        set(VCPKG_LUA_PATH "$ENV{USERPROFILE}/vcpkg/installed/${VCPKG_TRIPLET}")
        
        if(EXISTS "${VCPKG_LUA_PATH}/include/lua.h")
            message(STATUS "Found Lua in vcpkg at ${VCPKG_LUA_PATH}")
            set(LUA_INCLUDE_DIR "${VCPKG_LUA_PATH}/include")
            set(LUA_FOUND TRUE)
            
            # Find Lua library
            if(EXISTS "${VCPKG_LUA_PATH}/lib/lua.lib")
                set(LUA_LIBRARIES "${VCPKG_LUA_PATH}/lib/lua.lib")
                set(LUA_DLL_PATH "${VCPKG_LUA_PATH}/bin/lua.dll")
            elseif(EXISTS "${VCPKG_LUA_PATH}/lib/lua54.lib")
                set(LUA_LIBRARIES "${VCPKG_LUA_PATH}/lib/lua54.lib")
                set(LUA_DLL_PATH "${VCPKG_LUA_PATH}/bin/lua54.dll")
            elseif(EXISTS "${VCPKG_LUA_PATH}/lib/lua53.lib")
                set(LUA_LIBRARIES "${VCPKG_LUA_PATH}/lib/lua53.lib")
                set(LUA_DLL_PATH "${VCPKG_LUA_PATH}/bin/lua53.dll")
            else()
                message(WARNING "Lua include found but library not found in ${VCPKG_LUA_PATH}/lib/")
                set(LUA_FOUND FALSE)
            endif()
        else()
            # Try Scoop installation
            set(SCOOP_LUA_PATH "$ENV{USERPROFILE}/scoop/apps/lua/current")
            if(EXISTS "${SCOOP_LUA_PATH}/include/lua.h")
                message(STATUS "Found Lua in Scoop at ${SCOOP_LUA_PATH}")
                set(LUA_INCLUDE_DIR "${SCOOP_LUA_PATH}/include")
                set(LUA_DLL_PATH "${SCOOP_LUA_PATH}/bin/lua54.dll")
                # Scoop only ships MinGW .a files; need extern/lua/lua54.lib import lib
                if(EXISTS "${BUNDLED_LUA_PATH}/lua54.lib")
                    set(LUA_LIBRARIES "${BUNDLED_LUA_PATH}/lua54.lib")
                    set(LUA_FOUND TRUE)
                else()
                    message(WARNING "Scoop Lua found but no MSVC import library. Run: dumpbin/lib to create extern/lua/lua54.lib")
                    set(LUA_FOUND FALSE)
                endif()
            else()
                message(STATUS "Lua not found in vcpkg or Scoop either")
                message(WARNING "Lua not found! Lua scripting will be disabled.")
                message(STATUS "To enable Lua support:")
                message(STATUS "  Option 1: vcpkg install lua:x64-windows")
                message(STATUS "  Option 2: scoop install lua")
                message(STATUS "  Option 3: Place lua.h + lua54.lib in extern/lua/")
                
                set(LUA_INCLUDE_DIR "")
                set(LUA_LIBRARIES "")
            endif()
        endif()
    endif()
else()
    message(STATUS "Found Lua: ${LUA_LIBRARIES}")
    if(WIN32)
        list(GET LUA_LIBRARIES 0 _lua_import_library)
        get_filename_component(_lua_library_dir "${_lua_import_library}" DIRECTORY)
        get_filename_component(_lua_library_name "${_lua_import_library}" NAME_WE)
        find_file(LUA_DLL_PATH "${_lua_library_name}.dll"
            HINTS "${_lua_library_dir}/../bin" "${_lua_library_dir}" NO_DEFAULT_PATH)
    endif()
endif()

# Jolt Physics Engine
# ─────────────────────────────────────────────────────────────────────────────
# Build Configuration Options
# ─────────────────────────────────────────────────────────────────────────────
option(HEPHAESTUS_BUILD_EDITOR "Build the editor (HephaestusEngine.exe)" ON)
option(HEPHAESTUS_BUILD_GAME "Build the standalone game runtime (HephaestusGame.exe)" ON)
option(HEPHAESTUS_GAME_SCRIPTING "Start scripts (Lua and plugin languages) in the game runtime (Lua support itself follows LUA_FOUND)" ON)
option(HEPHAESTUS_USE_QT "Enable Qt-based editor UI" ON)
option(HEPHAESTUS_USE_SHARED_ASSETS "Use shared runtime/editor/launcher asset folders" ON)
option(HEPHAESTUS_MCP_LEGACY_COMPAT "Enable MCP 2025-11-25 compatibility" ON)
option(HEPHAESTUS_RUNTIME_AUTOMATION "Compile MCP automation into the packaged game runtime" OFF)

# Qt (Windows) - use C:\Qt as a hint for discovery
set(HEPHAESTUS_QT_ROOT "C:/Qt" CACHE PATH "Root path to Qt installation")
if(HEPHAESTUS_USE_QT)
    # Try to auto-locate Qt6Config.cmake under common Qt layout
    # IMPORTANT: Must use MSVC builds when compiling with Visual Studio
    if(EXISTS "${HEPHAESTUS_QT_ROOT}" AND NOT Qt6_DIR)
        # First try to find MSVC builds for the target architecture
        if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
            set(_qt6_msvc_arch "arm64")
        else()
            set(_qt6_msvc_arch "64")
        endif()
        file(GLOB _qt6_msvc_candidates
            "${HEPHAESTUS_QT_ROOT}/6.*/msvc*_${_qt6_msvc_arch}/lib/cmake/Qt6/Qt6Config.cmake"
        )
        list(LENGTH _qt6_msvc_candidates _qt6_msvc_count)
        if(_qt6_msvc_count GREATER 0)
            # Sort to get the latest version and prefer newer MSVC
            list(SORT _qt6_msvc_candidates)
            list(GET _qt6_msvc_candidates -1 _qt6_config)
            get_filename_component(Qt6_DIR "${_qt6_config}" DIRECTORY)
            message(STATUS "Auto-detected Qt6 (MSVC): ${Qt6_DIR}")
        else()
            # Check if only MinGW builds exist (incompatible with MSVC)
            file(GLOB _qt6_mingw_candidates
                "${HEPHAESTUS_QT_ROOT}/6.*/mingw*/lib/cmake/Qt6/Qt6Config.cmake"
            )
            list(LENGTH _qt6_mingw_candidates _qt6_mingw_count)
            if(_qt6_mingw_count GREATER 0 AND MSVC)
                message(WARNING "Found Qt6 MinGW builds but building with MSVC. These are incompatible!")
                message(WARNING "Please install Qt6 MSVC 64-bit using Qt Online Installer.")
                message(WARNING "Qt editor UI will be disabled for this build.")
                set(HEPHAESTUS_USE_QT OFF)
            endif()
        endif()
    endif()

    if(HEPHAESTUS_USE_QT AND Qt6_DIR)
        set(Qt6_DIR "${Qt6_DIR}" CACHE PATH "Path to Qt6Config.cmake directory" FORCE)
    endif()

    if(HEPHAESTUS_USE_QT)
        find_package(Qt6 COMPONENTS Core Widgets QUIET)
        if(Qt6_FOUND)
            find_package(Qt6 COMPONENTS Svg QUIET)
            find_package(Qt6 COMPONENTS Concurrent QUIET)
            find_package(Qt6 COMPONENTS Network QUIET)
        endif()
    endif()

    # MSVC requires /Zc:__cplusplus for Qt to detect C++ standard correctly
    if(Qt6_FOUND AND MSVC)
        add_compile_options(/Zc:__cplusplus)
    endif()
endif()

# Jolt Physics Engine
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build Jolt as static library" FORCE)
set(JOLT_SANITIZE_UNDEFINED_BEHAVIOR OFF CACHE BOOL "" FORCE)
set(JOLT_CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "Enable deterministic behavior" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "Disable Jolt warnings-as-errors in superproject builds" FORCE)
# The engine subclasses Jolt types (CharacterVirtual) with RTTI on; clang needs Jolt's typeinfo to link.
set(CPP_RTTI_ENABLED ON CACHE BOOL "Enable C++ RTTI in Jolt" FORCE)
# Disable static MSVC runtime to avoid linker conflicts
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Use dynamic MSVC runtime" FORCE)
add_subdirectory(extern/JoltPhysics/Build)

# Shared gameplay input core used by the Qt editor, packaged runtime, launcher
# settings UI, and focused headless tests.
add_library(HephaestusInputCore STATIC
    engine/Input/InputMap.cpp
    engine/Input/InputSystem.cpp
    engine/Input/GlfwGamepad.cpp
)
target_include_directories(HephaestusInputCore PUBLIC engine extern extern/glfw/include)
target_link_libraries(HephaestusInputCore PUBLIC glfw)

# Transport-independent automation registry and main-thread dispatch core.
# This library is intentionally free of any MCP/HTTP protocol implementation;
# semantic automation tools live here and are consumed by hosts and plugins.
add_library(HephaestusAutomationCore STATIC
    engine/Automation/AutomationTypes.cpp
    engine/Automation/AutomationService.cpp
    engine/Automation/ComponentMetadata.cpp
    engine/Automation/EntityRef.cpp
    engine/Automation/EditorAutomation.cpp
    engine/Automation/AssetAutomation.cpp
    engine/Automation/CharacterAutomation.cpp
    engine/Automation/LogAutomation.cpp
    engine/Automation/MaterialAutomation.cpp
    engine/Automation/PlayAutomation.cpp
    engine/Automation/RuntimeAutomation.cpp
    engine/Automation/SceneAutomation.cpp
    engine/Automation/ScriptAutomation.cpp
    engine/Automation/ToolRegistry.cpp
    engine/Automation/UIAutomation.cpp
    engine/Automation/PhysicsAutomation.cpp
    engine/Automation/ViewportAutomation.cpp
    engine/Automation/BuildAutomation.cpp
    engine/Automation/TestAutomation.cpp
)
target_include_directories(HephaestusAutomationCore PUBLIC
    engine
    extern
    extern/entt/single_include
)

# MCP protocol and HTTP transport implementation. Qt-free and usable by both
# the editor plugin and the packaged Development runtime. Depends only on the
# semantic automation core.
add_library(HephaestusMcpCore STATIC
    Plugins/HephaestusMCP/Core/McpHttpTypes.cpp
    Plugins/HephaestusMCP/Core/McpJsonRpc.cpp
    Plugins/HephaestusMCP/Core/McpToolSchema.cpp
    Plugins/HephaestusMCP/Core/McpSession.cpp
    Plugins/HephaestusMCP/Core/McpLegacySession.cpp
    Plugins/HephaestusMCP/Core/McpHttpTransport.cpp
    Plugins/HephaestusMCP/Core/McpServer.cpp
)
target_link_libraries(HephaestusMcpCore PUBLIC HephaestusAutomationCore)
target_include_directories(HephaestusMcpCore PUBLIC
    engine
    extern
    Plugins
)
target_compile_definitions(HephaestusMcpCore PUBLIC
    HEPHAESTUS_MCP_LEGACY_COMPAT=$<BOOL:${HEPHAESTUS_MCP_LEGACY_COMPAT}>
)

# Stable, Qt-independent plugin discovery/lifecycle and mounted-content core.
add_library(HephaestusPluginCore STATIC
    engine/Assets/AssetMountRegistry.cpp
    engine/Assets/AssetPathPolicy.cpp
    engine/Assets/AssetReferenceIndex.cpp
    engine/Plugins/PluginManifest.cpp
    engine/Plugins/PluginContent.cpp
    engine/Plugins/PluginSystem.cpp
)
target_include_directories(HephaestusPluginCore PUBLIC engine extern)

# ─────────────────────────────────────────────────────────────────────────────
# HephaestusRuntime: the Qt-free engine shared by the editor, the packaged game
# and the tests. Never link Qt here.
# ─────────────────────────────────────────────────────────────────────────────
add_library(HephaestusRuntime STATIC
    engine/Audio/AudioEngine.cpp
    engine/Audio/AudioMath.cpp
    engine/Audio/MiniaudioImplementation.c
    engine/Assets/GltfDocument.cpp
    engine/Assets/ModelImporter.cpp
    engine/Assets/FbxDocument.cpp
    engine/Assets/FbxStaticImporter.cpp
    engine/Assets/FbxSkeletalImporter.cpp
    engine/Assets/AssetDatabase.cpp
    engine/Assets/AssetImporters.cpp
    engine/Assets/AssetManager.cpp
    engine/Assets/CharacterAsset.cpp
    engine/Assets/CharacterRuntime.cpp
    engine/Assets/CharacterRuntimeGpu.cpp
    engine/Assets/CharacterImport.cpp
    engine/Assets/ModelMaterialImport.cpp
    engine/Assets/SkeletalModelImporter.cpp
    engine/Animation/Skeleton.cpp
    engine/Animation/AnimationClip.cpp
    engine/Animation/CharacterPlayback.cpp
    engine/Core/ProjectContext.cpp
    engine/Core/EngineLog.cpp
    engine/Core/AppPaths.cpp
    engine/Core/IniFile.cpp
    engine/Core/SimulationStep.cpp
    engine/Gfx/VulkanDevice.cpp
    engine/Gfx/VulkanSwapchain.cpp
    engine/Gfx/VulkanPipeline.cpp
    engine/Gfx/VulkanTexture.cpp
    engine/Gfx/VulkanGizmo.cpp
    engine/Gfx/VulkanLightGizmo.cpp
    engine/Gfx/BuiltinMeshes.cpp
    engine/Gfx/SkyIrradiance.cpp
    engine/Gfx/ProbeBake.cpp
    engine/Gfx/ProbeGI.cpp
    engine/Gfx/ProbeRayTracer.cpp
    engine/Gfx/EngineFrameRenderer.cpp
    engine/Physics/MeshCollisionSystem.cpp
    engine/Physics/PhysicsLayerConfig.cpp
    engine/Physics/JoltPhysicsEngine.cpp
    engine/Physics/JoltPhysicsSystem.cpp
    engine/Physics/RagdollSystem.cpp
    engine/Physics/CharacterMotor.cpp
    engine/Physics/CharacterControllerMath.cpp
    engine/Physics/CharacterControllerSystem.cpp
    engine/Scene/ActiveCameraSync.cpp
    engine/Scene/EditorIcons.cpp
    engine/Scene/Scene.cpp
    engine/Scene/SceneAuthoring.cpp
    engine/Scene/Entity.cpp
    engine/Scene/Json.cpp
    engine/Scene/PrefabSerializer.cpp
    engine/Scene/PrefabInstantiator.cpp
    engine/Scene/PrefabComponentIO.cpp
    engine/Scene/ParticleSimulation.cpp
    engine/Navigation/NavMesh.cpp
    engine/Scene/PrefabOverrideSystem.cpp
    engine/Scene/SceneSerializer.cpp
    engine/Scripting/ScriptBase.cpp
    engine/Scripting/ScriptEngine.cpp
    engine/Scripting/LuaScriptEngine.cpp
    engine/Scripting/LuaExposeScanner.cpp
    engine/Scripting/LuaScriptVariableReconciler.cpp
    engine/Scripting/ScriptPhysicsBindings.cpp
    engine/Scripting/ScriptLanguageRegistry.cpp
    engine/Scripting/LuaDebugger.cpp
    engine/Scripting/LuaLanguageService.cpp
    engine/Scripting/PluginScriptHost.cpp
    engine/UI/HephaestusUI/HUIAsset.cpp
    engine/UI/HephaestusUI/UIButton.cpp
    engine/UI/HephaestusUI/UICanvas.cpp
    engine/UI/HephaestusUI/UIContext.cpp
    engine/UI/HephaestusUI/RuntimeUIInspection.cpp
    engine/UI/HephaestusUI/UIElement.cpp
    engine/UI/HephaestusUI/UIFont.cpp
    engine/UI/HephaestusUI/UIGridLayout.cpp
    engine/UI/HephaestusUI/UIHorizontalBox.cpp
    engine/UI/HephaestusUI/UIImage.cpp
    engine/UI/HephaestusUI/UIInputRouter.cpp
    engine/UI/HephaestusUI/UILayout.cpp
    engine/UI/HephaestusUI/UIPanel.cpp
    engine/UI/HephaestusUI/UIRenderer.cpp
    engine/UI/HephaestusUI/UIScreen.cpp
    engine/UI/HephaestusUI/UISimpleMenuExample.cpp
    engine/UI/HephaestusUI/UISystem.cpp
    engine/UI/HephaestusUI/UIText.cpp
    engine/UI/HephaestusUI/UITheme.cpp
    engine/UI/HephaestusUI/UIVerticalBox.cpp
    engine/UI/HephaestusUI/Rendering/HUIGeometryBuilder.cpp
    engine/UI/HephaestusUI/Rendering/UIFontAtlas.cpp
    engine/UI/HephaestusUI/Rendering/VulkanHUIRenderer.cpp
)
target_include_directories(HephaestusRuntime PUBLIC
    engine
    extern
    extern/VMA
    extern/entt/single_include
    extern/JoltPhysics
    extern/JoltPhysics/Build
    "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(HephaestusRuntime PUBLIC
    HephaestusInputCore HephaestusPluginCore Vulkan::Vulkan glfw Jolt ufbx RecastDetour
)
if(LUA_FOUND)
    if(TARGET Lua::Lua)
        target_link_libraries(HephaestusRuntime PUBLIC Lua::Lua)
    else()
        target_link_libraries(HephaestusRuntime PUBLIC ${LUA_LIBRARIES})
    endif()
    target_include_directories(HephaestusRuntime PUBLIC ${LUA_INCLUDE_DIR})
    target_compile_definitions(HephaestusRuntime PUBLIC LUA_AVAILABLE=1)
else()
    target_compile_definitions(HephaestusRuntime PUBLIC LUA_AVAILABLE=0)
endif()
if(APPLE)
    target_link_libraries(HephaestusRuntime PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
    target_link_libraries(HephaestusRuntime PUBLIC ${CMAKE_DL_LIBS} m pthread)
endif()

# Qt-free editor services and documents. Editor-only, but never Qt: Qt code
# lives in engine/UI/QtEditor and is compiled into HephaestusEngine.
add_library(HephaestusEditorCore STATIC
    engine/Core/EditorContext.cpp
    engine/Core/EditorCameraController.cpp
    engine/Core/PlayModeManager.cpp
    engine/Assets/OpenDocumentRegistry.cpp
    engine/Assets/AssetOperationService.cpp
    engine/Assets/DerivedDataCache.cpp
    engine/Assets/AssetThumbnailRenderer.cpp
    engine/Assets/EditorAssetPipeline.cpp
    engine/Assets/EditorAssetImportService.cpp
    engine/Assets/AssetIntake.cpp
    engine/Assets/EditorViewState.cpp
    engine/Assets/FileWatcher.cpp
    engine/Assets/RecoveryStore.cpp
    engine/Assets/ReimportManager.cpp
    engine/UI/EditorDocument.cpp
    engine/UI/CharacterDocument.cpp
    engine/UI/InspectorSelectionModel.cpp
    engine/UI/ComponentClipboard.cpp
    engine/UI/MaterialEditor/MaterialGraph.cpp
    engine/UI/MaterialEditor/MaterialNodeDefinitions.cpp
    engine/UI/MaterialEditor/MaterialGraphEvaluator.cpp
    engine/UI/MaterialEditor/MaterialShaderGenerator.cpp
    engine/UI/MaterialEditor/MaterialAssetSerializer.cpp
    engine/UI/MaterialEditor/TextureDiscovery.cpp
)
target_link_libraries(HephaestusEditorCore PUBLIC HephaestusRuntime)

# Shared non-Qt project validation and packaging pipeline used by the editor
# and headless CLI. Keep compiler/process orchestration out of Qt UI code.
add_library(HephaestusBuildCore STATIC
    engine/Build/BuildManifest.cpp
    engine/Build/BuildService.cpp
    engine/Build/PackageStager.cpp
    engine/Build/ProjectValidator.cpp
    engine/Build/RuntimeLocator.cpp
)
target_include_directories(HephaestusBuildCore PUBLIC engine extern)
target_link_libraries(HephaestusBuildCore PUBLIC HephaestusPluginCore)

add_executable(HephaestusBuildCli engine/BuildCli.cpp)
target_link_libraries(HephaestusBuildCli PRIVATE HephaestusBuildCore)
target_include_directories(HephaestusBuildCli PRIVATE engine extern "${CMAKE_BINARY_DIR}/generated")
set_target_properties(HephaestusBuildCli PROPERTIES
    OUTPUT_NAME "HephaestusBuild"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/tools"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/tools"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/tools"
)

# ─────────────────────────────────────────────────────────────────────────────
# HephaestusToolsCommon (shared launcher/editor utilities)
# ─────────────────────────────────────────────────────────────────────────────
if(HEPHAESTUS_USE_QT AND Qt6_FOUND)
    add_library(HephaestusToolsCommon STATIC
        engine/ToolsCommon/SharedAssetPaths.h
        engine/ToolsCommon/SharedAssetPaths.cpp
        engine/ToolsCommon/PathUtils.h
        engine/ToolsCommon/PathUtils.cpp
        engine/ToolsCommon/JsonUtils.h
        engine/ToolsCommon/JsonUtils.cpp
        engine/ToolsCommon/EngineContext.h
        engine/ToolsCommon/SettingsSchema.h
        engine/ToolsCommon/SettingsSchema.cpp
        engine/ToolsCommon/EngineSettingsManager.h
        engine/ToolsCommon/EngineSettingsManager.cpp
        engine/ToolsCommon/ProjectSettingsManager.h
        engine/ToolsCommon/ProjectSettingsManager.cpp
        engine/ToolsCommon/ProjectCreationService.h
        engine/ToolsCommon/ProjectCreationService.cpp
        engine/ToolsCommon/VersionInfo.h
        engine/ToolsCommon/VersionInfo.cpp
    )

    target_link_libraries(HephaestusToolsCommon PUBLIC Qt6::Core HephaestusInputCore)
    target_include_directories(HephaestusToolsCommon PUBLIC engine "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(HephaestusToolsCommon PUBLIC
        $<$<BOOL:${HEPHAESTUS_USE_SHARED_ASSETS}>:HEPHAESTUS_USE_SHARED_ASSETS=1>
        $<$<NOT:$<BOOL:${HEPHAESTUS_USE_SHARED_ASSETS}>>:HEPHAESTUS_USE_SHARED_ASSETS=0>
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# HephaestusEngine (Editor) Target
# ─────────────────────────────────────────────────────────────────────────────
if(HEPHAESTUS_BUILD_EDITOR)
add_executable(HephaestusEngine
    engine/Core/main.cpp
)

if (WIN32)
    # Raster sizes of Branding/logo/pngs/HephaestusEngineLogo_Main.png, the same logo as the runtime window icon.
    set(APP_ICON_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Branding/logo/Editor")
    set(APP_ICON_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_app_icon.ps1")
    set(APP_ICON_OUTPUT "${CMAKE_BINARY_DIR}/HephaestusEngine.ico")

    add_custom_command(
        OUTPUT ${APP_ICON_OUTPUT}
        COMMAND powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "${APP_ICON_SCRIPT}"
                -SourceDir "${APP_ICON_SOURCE_DIR}"
                -OutputIcon "${APP_ICON_OUTPUT}"
        DEPENDS
            "${APP_ICON_SCRIPT}"
            "${APP_ICON_SOURCE_DIR}/256px.png"
            "${APP_ICON_SOURCE_DIR}/128px.png"
            "${APP_ICON_SOURCE_DIR}/64px.png"
            "${APP_ICON_SOURCE_DIR}/48px.png"
            "${APP_ICON_SOURCE_DIR}/32px.png"
            "${APP_ICON_SOURCE_DIR}/16px.png"
        COMMENT "Generating Windows application icon (.ico)"
        VERBATIM
    )

    add_custom_target(GenerateAppIcon ALL DEPENDS ${APP_ICON_OUTPUT})

    set(APP_ICON_RC "${CMAKE_BINARY_DIR}/HephaestusEngine_icon.rc")
    set(APP_ICON_PATH ${APP_ICON_OUTPUT})
    configure_file(engine/Core/HephaestusEngine.rc.in ${APP_ICON_RC} @ONLY)

    add_dependencies(HephaestusEngine GenerateAppIcon)
    target_sources(HephaestusEngine PRIVATE ${APP_ICON_RC})
    set_source_files_properties(${APP_ICON_RC} PROPERTIES GENERATED TRUE)
endif()

target_link_libraries(HephaestusEngine PRIVATE HephaestusEditorCore HephaestusBuildCore HephaestusAutomationCore)

# Copy Lua DLL to output directory
if(LUA_FOUND AND DEFINED LUA_DLL_PATH AND EXISTS "${LUA_DLL_PATH}")
    get_filename_component(LUA_DLL_NAME "${LUA_DLL_PATH}" NAME)
    add_custom_command(TARGET HephaestusEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LUA_DLL_PATH}"
            "$<TARGET_FILE_DIR:HephaestusEngine>/${LUA_DLL_NAME}"
        COMMENT "Copying ${LUA_DLL_NAME} to output directory"
    )
endif()


# Qt editor UI (optional)
if(HEPHAESTUS_USE_QT)
    if(Qt6_FOUND)
        # Enable Qt auto-processing for moc, uic, rcc
        set_target_properties(HephaestusEngine PROPERTIES
            AUTOMOC ON
            AUTOUIC ON
            AUTORCC ON
        )

        # Add Qt editor source files
        target_sources(HephaestusEngine PRIVATE
            engine/UI/QtEditor/EditorQtApp.cpp
            engine/UI/QtEditor/EditorQtApp.h
            engine/UI/QtEditor/CodeEditorPanel.cpp
            engine/UI/QtEditor/CodeEditorPanel.h
            engine/UI/QtEditor/DebuggerPanel.cpp
            engine/UI/QtEditor/DebuggerPanel.h
            engine/UI/QtEditor/ProjectSearchPanel.cpp
            engine/UI/QtEditor/ProjectSearchPanel.h
            engine/UI/QtEditor/EditorCommandSearch.cpp
            engine/UI/QtEditor/EditorCommandSearch.h
            engine/UI/QtEditor/EditorDockTitleBar.cpp
            engine/UI/QtEditor/EditorDockTitleBar.h
            engine/UI/QtEditor/EditorConsolePanel.cpp
            engine/UI/QtEditor/EditorConsolePanel.h
            engine/UI/QtEditor/EditorTheme.cpp
            engine/UI/QtEditor/EditorTheme.h
            engine/UI/QtEditor/EditorSystemUsage.cpp
            engine/UI/QtEditor/EditorSystemUsage.h
            engine/UI/QtEditor/Resources/EditorResources.qrc
            engine/UI/QtEditor/EditorMainWindow.cpp
            engine/UI/QtEditor/EditorMainWindow.h
            engine/UI/QtEditor/QtViewportWindow.cpp
            engine/UI/QtEditor/QtViewportWindow.h
            engine/UI/QtEditor/QtEditorEngine.cpp
            engine/UI/QtEditor/QtEditorEngine.h
            engine/UI/QtEditor/RuntimeUIOverlayWidget.cpp
            engine/UI/QtEditor/RuntimeUIOverlayWidget.h
            engine/UI/QtEditor/AssetBrowserWidget.cpp
            engine/UI/QtEditor/AssetBrowserWidget.h
            engine/UI/QtEditor/AssetDependencyDialog.cpp
            engine/UI/QtEditor/AssetDependencyDialog.h
            engine/UI/QtEditor/AssetThumbnailCache.cpp
            engine/UI/QtEditor/AssetThumbnailCache.h
            engine/UI/QtEditor/EditorAssetServices.cpp
            engine/UI/QtEditor/EditorAssetServices.h
            engine/UI/QtEditor/EditorPluginHost.cpp
            engine/UI/QtEditor/EditorPluginHost.h
            engine/UI/QtEditor/PluginManagerDialog.cpp
            engine/UI/QtEditor/PluginManagerDialog.h
            engine/UI/QtEditor/EditorAssetActions.cpp
            engine/UI/QtEditor/EditorAssetActions.h
            engine/UI/QtEditor/TextureViewerDialog.cpp
            engine/UI/QtEditor/TextureViewerDialog.h
            engine/UI/QtEditor/HUIVisualEditorDialog.cpp
            engine/UI/QtEditor/HUIVisualEditorDialog.h
            engine/UI/QtEditor/MaterialGraphQtEditorDialog.cpp
            engine/UI/QtEditor/MaterialGraphQtEditorDialog.h
            engine/UI/QtEditor/LightInspector.cpp
            engine/UI/QtEditor/LightInspector.h
            engine/UI/QtEditor/EnvironmentInspector.cpp
            engine/UI/QtEditor/EnvironmentInspector.h
            engine/UI/QtEditor/ComponentInspector.h
            engine/UI/QtEditor/PropertyEditors.cpp
            engine/UI/QtEditor/PropertyEditors.h
            engine/UI/QtEditor/InspectorMime.cpp
            engine/UI/QtEditor/InspectorMime.h
            engine/UI/QtEditor/AssetPickerDialog.cpp
            engine/UI/QtEditor/AssetPickerDialog.h
            engine/UI/QtEditor/AssetReferenceEditor.cpp
            engine/UI/QtEditor/AssetReferenceEditor.h
            engine/UI/QtEditor/AddComponentPopup.cpp
            engine/UI/QtEditor/AddComponentPopup.h
            engine/UI/QtEditor/AudioSourceInspector.cpp
            engine/UI/QtEditor/AudioSourceInspector.h
            engine/UI/QtEditor/ParticleEmitterInspector.cpp
            engine/UI/QtEditor/ParticleEmitterInspector.h
            engine/UI/QtEditor/EditorUndoCommand.cpp
            engine/UI/QtEditor/EditorUndoCommand.h
            engine/UI/QtEditor/MeshRendererInspector.cpp
            engine/UI/QtEditor/MeshRendererInspector.h
            engine/UI/QtEditor/ScriptInspector.cpp
            engine/UI/QtEditor/ScriptInspector.h
            engine/UI/QtEditor/LuaScriptInspector.cpp
            engine/UI/QtEditor/LuaScriptInspector.h
            engine/UI/QtEditor/CharacterInspector.cpp
            engine/UI/QtEditor/CharacterInspector.h
            engine/UI/QtEditor/CharacterControllerInspector.cpp
            engine/UI/QtEditor/CharacterControllerInspector.h
            engine/UI/QtEditor/ProbeVolumeInspector.cpp
            engine/UI/QtEditor/ProbeVolumeInspector.h
            engine/UI/QtEditor/CharacterImportDialog.cpp
            engine/UI/QtEditor/CharacterImportDialog.h
            engine/UI/QtEditor/PhysicsInspectors.cpp
            engine/UI/QtEditor/PhysicsInspectors.h
            engine/UI/QtEditor/CameraInspector.cpp
            engine/UI/QtEditor/CameraInspector.h
            engine/UI/QtEditor/AddActorsPanel.cpp
            engine/UI/QtEditor/AddActorsPanel.h
            engine/UI/QtEditor/BuildSettingsDialog.cpp
            engine/UI/QtEditor/BuildSettingsDialog.h
            engine/UI/QtEditor/AboutDialog.cpp
            engine/UI/QtEditor/AboutDialog.h
            engine/UI/QtEditor/EditorNotificationService.cpp
            engine/UI/QtEditor/EditorNotificationService.h
            engine/UI/QtEditor/PrefabEditorWindow.cpp
            engine/UI/QtEditor/PrefabEditorWindow.h
            engine/UI/QtEditor/PrefabEditorManager.cpp
            engine/UI/QtEditor/PrefabEditorManager.h
            engine/UI/QtEditor/CharacterEditorWindow.cpp
            engine/UI/QtEditor/CharacterEditorWindow.h
            engine/UI/QtEditor/CharacterEditorManager.cpp
            engine/UI/QtEditor/CharacterEditorManager.h
            engine/UI/QtEditor/CharacterTimelineWidget.cpp
            engine/UI/QtEditor/CharacterTimelineWidget.h
            engine/UI/QtEditor/CharacterAnimationImportDialog.cpp
            engine/UI/QtEditor/CharacterAnimationImportDialog.h
            engine/UI/QtEditor/PrefabHierarchyPanel.cpp
            engine/UI/QtEditor/HierarchyEye.cpp
            engine/UI/QtEditor/PrefabHierarchyPanel.h
            engine/UI/QtEditor/PrefabInspectorPanel.cpp
            engine/UI/QtEditor/PrefabInspectorPanel.h
            engine/UI/QtEditor/PrefabInstanceBar.cpp
            engine/UI/QtEditor/PrefabInstanceBar.h
            engine/UI/Shared/SettingsWindow.cpp
            engine/UI/Shared/SettingsWindow.h
            engine/UI/Shared/InputMapEditorWidget.cpp
            engine/UI/Shared/InputMapEditorWidget.h
            engine/UI/Shared/PhysicsLayerEditorWidget.cpp
            engine/UI/Shared/PhysicsLayerEditorWidget.h
        )

        if(APPLE)
            target_sources(HephaestusEngine PRIVATE
                engine/UI/QtEditor/MacNativeSurface.h
                engine/UI/QtEditor/MacNativeSurface.mm
            )
        endif()

        if(NOT TARGET Qt6::Svg)
            message(FATAL_ERROR "The Qt editor requires the Qt6 Svg module for its icon set.")
        endif()
        target_link_libraries(HephaestusEngine PRIVATE Qt6::Widgets Qt6::Svg)
        if(TARGET HephaestusToolsCommon)
            target_link_libraries(HephaestusEngine PRIVATE HephaestusToolsCommon)
        endif()
        target_compile_definitions(HephaestusEngine PRIVATE HEPHAESTUS_EDITOR_QT_UI=1)

        add_library(HephaestusPrimitiveTools SHARED
            Plugins/PrimitiveTools/Source/PrimitiveToolsPlugin.cpp
        )
        target_include_directories(HephaestusPrimitiveTools PRIVATE engine)
        target_link_libraries(HephaestusPrimitiveTools PRIVATE Qt6::Widgets)
        set_target_properties(HephaestusPrimitiveTools PROPERTIES
            OUTPUT_NAME "HephaestusPrimitiveTools"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/Plugins/PrimitiveTools/bin"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/Plugins/PrimitiveTools/bin"
        )
        add_custom_command(TARGET HephaestusPrimitiveTools POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/PrimitiveTools/PrimitiveTools.hpplugin"
                "$<TARGET_FILE_DIR:HephaestusPrimitiveTools>/../PrimitiveTools.hpplugin"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/PrimitiveTools/Content"
                "$<TARGET_FILE_DIR:HephaestusPrimitiveTools>/../Content"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/PrimitiveTools/Resources"
                "$<TARGET_FILE_DIR:HephaestusPrimitiveTools>/../Resources"
            VERBATIM
        )
        add_dependencies(HephaestusEngine HephaestusPrimitiveTools)

        add_library(HephaestusFPS SHARED
            Plugins/HephaestusFPS/Source/HephaestusFPSEditor.cpp
        )
        target_include_directories(HephaestusFPS PRIVATE engine extern)
        target_link_libraries(HephaestusFPS PRIVATE Qt6::Widgets)
        set_target_properties(HephaestusFPS PROPERTIES
            OUTPUT_NAME "HephaestusFPS"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/Plugins/HephaestusFPS/bin"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/Plugins/HephaestusFPS/bin"
        )
        add_custom_command(TARGET HephaestusFPS POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/HephaestusFPS/HephaestusFPS.hpplugin"
                "$<TARGET_FILE_DIR:HephaestusFPS>/../HephaestusFPS.hpplugin"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/HephaestusFPS/Content"
                "$<TARGET_FILE_DIR:HephaestusFPS>/../Content"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/HephaestusFPS/Resources"
                "$<TARGET_FILE_DIR:HephaestusFPS>/../Resources"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/HephaestusFPS/Documentation"
                "$<TARGET_FILE_DIR:HephaestusFPS>/../Documentation"
            VERBATIM
        )
        add_dependencies(HephaestusEngine HephaestusFPS)

        # Asset store plugins: Fab packs from disk and Sketchfab over its Download API (needs Qt Network).
        foreach(_store Fab Sketchfab)
            if(_store STREQUAL "Sketchfab" AND NOT TARGET Qt6::Network)
                message(WARNING "Qt6 Network not found; the Sketchfab plugin is not built.")
                continue()
            endif()
            add_library(Hephaestus${_store} SHARED Plugins/${_store}/Source/${_store}Plugin.cpp)
            target_include_directories(Hephaestus${_store} PRIVATE engine)
            target_link_libraries(Hephaestus${_store} PRIVATE Qt6::Widgets)
            set_target_properties(Hephaestus${_store} PROPERTIES
                OUTPUT_NAME "Hephaestus${_store}"
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/Plugins/${_store}/bin"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/Plugins/${_store}/bin"
            )
            add_custom_command(TARGET Hephaestus${_store} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/${_store}/${_store}.hpplugin"
                    "$<TARGET_FILE_DIR:Hephaestus${_store}>/../${_store}.hpplugin"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/${_store}/Resources"
                    "$<TARGET_FILE_DIR:Hephaestus${_store}>/../Resources"
                VERBATIM
            )
            add_dependencies(HephaestusEngine Hephaestus${_store})
        endforeach()
        if(TARGET HephaestusSketchfab)
            target_link_libraries(HephaestusSketchfab PRIVATE Qt6::Network)
            if(WIN32)
                target_link_libraries(HephaestusSketchfab PRIVATE Advapi32)
            endif()
        endif()

        # Official MCP editor plugin. A first-party consumer of the Automation
        # API: it links the Qt-free McpCore and never the engine's internals.
        add_library(HephaestusMCP SHARED
            Plugins/HephaestusMCP/Source/HephaestusMcpPlugin.cpp
        )
        target_include_directories(HephaestusMCP PRIVATE engine)
        target_link_libraries(HephaestusMCP PRIVATE HephaestusMcpCore Qt6::Widgets)
        set_target_properties(HephaestusMCP PROPERTIES
            OUTPUT_NAME "HephaestusMCP"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/Plugins/HephaestusMCP/bin"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/Plugins/HephaestusMCP/bin"
        )
        add_custom_command(TARGET HephaestusMCP POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/HephaestusMCP/HephaestusMCP.hpplugin"
                "$<TARGET_FILE_DIR:HephaestusMCP>/../HephaestusMCP.hpplugin"
            VERBATIM
        )
        add_dependencies(HephaestusEngine HephaestusMCP)

        # Copy Qt DLLs to output directory (Windows)
        if(WIN32)
            # Get Qt binary directory
            get_target_property(_qt6_widgets_location Qt6::Widgets LOCATION)
            get_filename_component(_qt6_bin_dir "${_qt6_widgets_location}" DIRECTORY)

            add_custom_command(TARGET HephaestusEngine POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_qt6_bin_dir}/Qt6Core.dll"
                    "$<TARGET_FILE_DIR:HephaestusEngine>/Qt6Core.dll"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_qt6_bin_dir}/Qt6Gui.dll"
                    "$<TARGET_FILE_DIR:HephaestusEngine>/Qt6Gui.dll"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_qt6_bin_dir}/Qt6Widgets.dll"
                    "$<TARGET_FILE_DIR:HephaestusEngine>/Qt6Widgets.dll"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_qt6_bin_dir}/Qt6Svg.dll"
                    "$<TARGET_FILE_DIR:HephaestusEngine>/Qt6Svg.dll"
                COMMENT "Copying Qt6 DLLs to output directory"
            )
            if(TARGET HephaestusSketchfab)
                add_custom_command(TARGET HephaestusEngine POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_qt6_bin_dir}/Qt6Network.dll"
                        "$<TARGET_FILE_DIR:HephaestusEngine>/Qt6Network.dll"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HephaestusEngine>/tls"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_qt6_bin_dir}/../plugins/tls/qschannelbackend.dll"
                        "$<TARGET_FILE_DIR:HephaestusEngine>/tls/qschannelbackend.dll"
                    COMMENT "Copying Qt6 Network and its Windows TLS backend for the Sketchfab plugin"
                )
            endif()

            # Copy Qt platform plugin (required for Qt apps to run)
            add_custom_command(TARGET HephaestusEngine POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:HephaestusEngine>/platforms"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_qt6_bin_dir}/../plugins/platforms/qwindows.dll"
                    "$<TARGET_FILE_DIR:HephaestusEngine>/platforms/qwindows.dll"
                COMMENT "Copying Qt6 platform plugin"
            )
        endif()

        message(STATUS "Qt6 editor UI enabled. Qt sources added to HephaestusEngine.")
    else()
        message(WARNING "Qt6 Widgets not found. Set Qt6_DIR or adjust HEPHAESTUS_QT_ROOT to your Qt install.")
        target_compile_definitions(HephaestusEngine PRIVATE HEPHAESTUS_EDITOR_QT_UI=0)
    endif()
else()
    target_compile_definitions(HephaestusEngine PRIVATE HEPHAESTUS_EDITOR_QT_UI=0)
endif()

# Set output directory for executable
set_target_properties(HephaestusEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
)


endif() # End HEPHAESTUS_BUILD_EDITOR

# ─────────────────────────────────────────────────────────────────────────────
# HephaestusLauncher (Qt-based launcher)
# ─────────────────────────────────────────────────────────────────────────────
if(HEPHAESTUS_USE_QT AND Qt6_FOUND)
if(NOT TARGET Qt6::Svg)
    message(FATAL_ERROR "The Qt launcher requires the Qt6 Svg module for the editor icon set.")
endif()
add_executable(HephaestusLauncher
    engine/UI/Launcher/LauncherMain.cpp
    engine/UI/Launcher/LauncherProject.h
    engine/UI/Launcher/LauncherProjectManager.h
    engine/UI/Launcher/LauncherProjectManager.cpp
    engine/UI/Launcher/LauncherProjectScanner.h
    engine/UI/Launcher/LauncherProjectScanner.cpp
    engine/UI/Launcher/ProjectCardModel.h
    engine/UI/Launcher/ProjectCardModel.cpp
    engine/UI/Launcher/ProjectCardDelegate.h
    engine/UI/Launcher/ProjectCardDelegate.cpp
    engine/UI/Launcher/LauncherWindow.h
    engine/UI/Launcher/LauncherWindow.cpp
    engine/UI/QtEditor/EditorTheme.h
    engine/UI/QtEditor/EditorTheme.cpp
    engine/UI/QtEditor/Resources/EditorResources.qrc
    engine/UI/Launcher/LauncherResources.qrc
)

if(WIN32)
    set(LAUNCHER_ICON_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Branding/logo/Launcher")
    set(LAUNCHER_ICON_OUTPUT "${CMAKE_BINARY_DIR}/HephaestusLauncher.ico")

    add_custom_command(
        OUTPUT ${LAUNCHER_ICON_OUTPUT}
        COMMAND powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_app_icon.ps1"
                -SourceDir "${LAUNCHER_ICON_SOURCE_DIR}"
                -OutputIcon "${LAUNCHER_ICON_OUTPUT}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_app_icon.ps1"
            "${LAUNCHER_ICON_SOURCE_DIR}/256px.png"
            "${LAUNCHER_ICON_SOURCE_DIR}/128px.png"
            "${LAUNCHER_ICON_SOURCE_DIR}/64px.png"
            "${LAUNCHER_ICON_SOURCE_DIR}/48px.png"
            "${LAUNCHER_ICON_SOURCE_DIR}/32px.png"
            "${LAUNCHER_ICON_SOURCE_DIR}/16px.png"
        COMMENT "Generating launcher application icon (.ico)"
        VERBATIM
    )
    add_custom_target(GenerateLauncherIcon DEPENDS ${LAUNCHER_ICON_OUTPUT})

    set(LAUNCHER_ICON_RC "${CMAKE_BINARY_DIR}/HephaestusLauncher_icon.rc")
    set(APP_ICON_PATH ${LAUNCHER_ICON_OUTPUT})
    configure_file(engine/Core/HephaestusEngine.rc.in ${LAUNCHER_ICON_RC} @ONLY)

    add_dependencies(HephaestusLauncher GenerateLauncherIcon)
    target_sources(HephaestusLauncher PRIVATE ${LAUNCHER_ICON_RC})
    set_source_files_properties(${LAUNCHER_ICON_RC} PROPERTIES GENERATED TRUE)
endif()

set_target_properties(HephaestusLauncher PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
)

target_link_libraries(HephaestusLauncher PRIVATE
    Qt6::Widgets
    Qt6::Svg
    HephaestusToolsCommon
)
target_compile_definitions(HephaestusLauncher PRIVATE HEPHAESTUS_EDITOR_QT_UI=1)

if(TARGET Qt6::Concurrent)
    target_link_libraries(HephaestusLauncher PRIVATE Qt6::Concurrent)
endif()

if(WIN32)
    get_target_property(_qt6_widgets_location Qt6::Widgets LOCATION)
    get_filename_component(_qt6_bin_dir "${_qt6_widgets_location}" DIRECTORY)

    add_custom_command(TARGET HephaestusLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt6_bin_dir}/Qt6Core.dll"
            "$<TARGET_FILE_DIR:HephaestusLauncher>/Qt6Core.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt6_bin_dir}/Qt6Gui.dll"
            "$<TARGET_FILE_DIR:HephaestusLauncher>/Qt6Gui.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt6_bin_dir}/Qt6Widgets.dll"
            "$<TARGET_FILE_DIR:HephaestusLauncher>/Qt6Widgets.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt6_bin_dir}/Qt6Svg.dll"
            "$<TARGET_FILE_DIR:HephaestusLauncher>/Qt6Svg.dll"
        COMMENT "Copying Qt6 DLLs for launcher"
    )

    add_custom_command(TARGET HephaestusLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:HephaestusLauncher>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt6_bin_dir}/../plugins/platforms/qwindows.dll"
            "$<TARGET_FILE_DIR:HephaestusLauncher>/platforms/qwindows.dll"
        COMMENT "Copying Qt6 platform plugin for launcher"
    )

    if(TARGET Qt6::Svg)
        add_custom_command(TARGET HephaestusLauncher POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:HephaestusLauncher>/iconengines"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_qt6_bin_dir}/../plugins/iconengines/qsvgicon.dll"
                "$<TARGET_FILE_DIR:HephaestusLauncher>/iconengines/qsvgicon.dll"
            COMMENT "Copying Qt6 SVG icon engine plugin for launcher"
        )
    endif()
endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# HephaestusGame (Standalone Game Runtime) Target
# ─────────────────────────────────────────────────────────────────────────────
if(HEPHAESTUS_BUILD_GAME)

add_executable(HephaestusGame
    engine/Game/GameMain.cpp
    engine/Core/RuntimeBootstrap.cpp
)

set(HEPHAESTUS_GAME_ICON "" CACHE FILEPATH
    "Optional .ico file embedded when rebuilding HephaestusGame")
if(WIN32 AND HEPHAESTUS_GAME_ICON AND EXISTS "${HEPHAESTUS_GAME_ICON}")
    set(APP_ICON_PATH "${HEPHAESTUS_GAME_ICON}")
    set(GAME_ICON_RC "${CMAKE_BINARY_DIR}/HephaestusGame_icon.rc")
    configure_file(engine/Core/HephaestusEngine.rc.in "${GAME_ICON_RC}" @ONLY)
    target_sources(HephaestusGame PRIVATE "${GAME_ICON_RC}")
    set_source_files_properties("${GAME_ICON_RC}" PROPERTIES GENERATED TRUE)
endif()

if(HEPHAESTUS_RUNTIME_AUTOMATION)
    target_compile_definitions(HephaestusGame PRIVATE
        HEPHAESTUS_RUNTIME_AUTOMATION=1
        HEPHAESTUS_MCP_LEGACY_COMPAT=$<BOOL:${HEPHAESTUS_MCP_LEGACY_COMPAT}>
    )
    target_sources(HephaestusGame PRIVATE engine/Game/PackagedAutomation.cpp)
    target_link_libraries(HephaestusGame PRIVATE HephaestusMcpCore)
else()
    target_compile_definitions(HephaestusGame PRIVATE HEPHAESTUS_RUNTIME_AUTOMATION=0)
endif()

# Lua itself follows LUA_AVAILABLE, so plugin languages still run in a build without Lua.
if(HEPHAESTUS_GAME_SCRIPTING)
    target_compile_definitions(HephaestusGame PRIVATE HEPHAESTUS_GAME_SCRIPTING=1)
else()
    target_compile_definitions(HephaestusGame PRIVATE HEPHAESTUS_GAME_SCRIPTING=0)
endif()

target_link_libraries(HephaestusGame PRIVATE HephaestusRuntime)

# Windows: link dbghelp for crash handler
if(WIN32)
    target_link_libraries(HephaestusGame PRIVATE dbghelp)
endif()

# Set output directory for game executable
set_target_properties(HephaestusGame PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
)

# Copy Lua DLL to game output directory
if(LUA_FOUND AND WIN32)
    if(DEFINED LUA_DLL_PATH AND EXISTS "${LUA_DLL_PATH}")
        get_filename_component(LUA_DLL_NAME "${LUA_DLL_PATH}" NAME)
        add_custom_command(TARGET HephaestusGame POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LUA_DLL_PATH}"
                "$<TARGET_FILE_DIR:HephaestusGame>/${LUA_DLL_NAME}"
            COMMENT "Copying ${LUA_DLL_NAME} to game output directory"
        )
    endif()
endif()

# Depend on shaders
if(TARGET Shaders)
    add_dependencies(HephaestusGame Shaders)
endif()

endif() # End HEPHAESTUS_BUILD_GAME

# ─────────────────────────────────────────────────────────────────────────────
# Shader Compilation (shared by Editor and Game)
# ─────────────────────────────────────────────────────────────────────────────
find_program(GLSLC_EXECUTABLE NAMES glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin)
if (GLSLC_EXECUTABLE)
    message(STATUS "Found glslc: ${GLSLC_EXECUTABLE}")
    set(SHADER_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle_skinned.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/billboard.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/billboard.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/billboard_id.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/billboard_id.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/entityid.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/entityid.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/entityid_skinned.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gizmo.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/outline.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/outline.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/outline_skinned.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skybox.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skybox.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadowmap.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadowmap.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadowmap_skinned.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hui.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hui.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tonemap.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tonemap.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom.comp
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/probe_update.comp
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/probe_trace.comp
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/probe_debug.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/probe_debug.frag
    )

    set(SHADER_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/lighting_common.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/probe_common.glsl
    )

    set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/$<CONFIG>/shaders)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/Release/shaders)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/Debug/shaders)

    set(SHADER_OUTPUTS)
    foreach(SRC ${SHADER_SOURCES})
        get_filename_component(FN ${SRC} NAME)
        set(OUT ${SHADER_OUTPUT_DIR}/${FN}.spv)
        set(GLSLC_EXTRA_FLAGS)
        if(FN STREQUAL "probe_trace.comp")
            set(GLSLC_EXTRA_FLAGS --target-env=vulkan1.2)  # ray queries need SPIR-V 1.4+
        endif()
        add_custom_command(
            OUTPUT ${OUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
            COMMAND ${GLSLC_EXECUTABLE} ${GLSLC_EXTRA_FLAGS} ${SRC} -o ${OUT}
            DEPENDS ${SRC} ${SHADER_INCLUDES}
            COMMENT "Compiling ${FN} -> ${OUT}"
            VERBATIM
        )
        list(APPEND SHADER_OUTPUTS ${OUT})
    endforeach()

    # Surface shader with ray-traced shadows, used on GPUs with VK_KHR_ray_query (EngineFrameRenderer::surfaceFragmentShader).
    set(RT_FRAG_OUT ${SHADER_OUTPUT_DIR}/triangle_rt.frag.spv)
    add_custom_command(
        OUTPUT ${RT_FRAG_OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
        COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.2 -DRAY_TRACED_SHADOWS
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag -o ${RT_FRAG_OUT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag ${SHADER_INCLUDES}
        COMMENT "Compiling triangle.frag (ray-traced shadows) -> ${RT_FRAG_OUT}"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS ${RT_FRAG_OUT})

    add_custom_target(Shaders ALL DEPENDS ${SHADER_OUTPUTS})
    
    # Add shader dependency to both targets
    if(TARGET HephaestusEngine)
        add_dependencies(HephaestusEngine Shaders)
    endif()
    if(TARGET HephaestusGame)
        add_dependencies(HephaestusGame Shaders)
    endif()
else()
    message(WARNING "glslc not found; expecting precompiled .spv under build/<config>/shaders")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Shared Post-Build Assets Copy (Editor/Game/Launcher)
# ─────────────────────────────────────────────────────────────────────────────
function(hephaestus_add_shared_assets_copy target_name)
    if(NOT TARGET ${target_name})
        return()
    endif()
    # macOS .app bundles read these from the folder holding the bundle (see AppPaths.h).
    if(APPLE)
        set(asset_dir "${CMAKE_BINARY_DIR}/$<CONFIG>")
    else()
        set(asset_dir "$<TARGET_FILE_DIR:${target_name}>")
    endif()

    if(HEPHAESTUS_USE_SHARED_ASSETS)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "${asset_dir}/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/editor"
                "${asset_dir}/editor"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
                "${asset_dir}/scripts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/.engine/meshes"
                "${asset_dir}/.engine/meshes"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md"
                "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
                "${asset_dir}"
            COMMENT "Copying shared runtime/editor assets for ${target_name}"
        )
    else()
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "${asset_dir}/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
                "${asset_dir}/scripts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/.engine/meshes"
                "${asset_dir}/.engine/meshes"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md"
                "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
                "${asset_dir}"
            COMMENT "Copying runtime assets for ${target_name}"
        )
    endif()
endfunction()

hephaestus_add_shared_assets_copy(HephaestusEngine)
hephaestus_add_shared_assets_copy(HephaestusGame)
hephaestus_add_shared_assets_copy(HephaestusLauncher)

# ─────────────────────────────────────────────────────────────────────────────
# macOS .app bundles (Editor/Game/Launcher)
# ─────────────────────────────────────────────────────────────────────────────
if(APPLE)
    # Compile the Icon Composer icon to Assets.car (Liquid Glass) plus a fallback .icns.
    set(HEPHAESTUS_MAC_ICON_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Branding/logo/MacOS-Icon.icon")
    set(HEPHAESTUS_MAC_ICON_DIR "${CMAKE_BINARY_DIR}/MacIcon")
    set(HEPHAESTUS_MAC_ICON_FILES
        "${HEPHAESTUS_MAC_ICON_DIR}/Assets.car"
        "${HEPHAESTUS_MAC_ICON_DIR}/MacOS-Icon.icns")
    file(GLOB HEPHAESTUS_MAC_ICON_INPUTS "${HEPHAESTUS_MAC_ICON_SOURCE}/*" "${HEPHAESTUS_MAC_ICON_SOURCE}/Assets/*")
    add_custom_command(
        OUTPUT ${HEPHAESTUS_MAC_ICON_FILES}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HEPHAESTUS_MAC_ICON_DIR}"
        COMMAND xcrun actool "${HEPHAESTUS_MAC_ICON_SOURCE}" --compile "${HEPHAESTUS_MAC_ICON_DIR}"
                --platform macosx --minimum-deployment-target ${CMAKE_OSX_DEPLOYMENT_TARGET}
                --app-icon MacOS-Icon
                --output-partial-info-plist "${HEPHAESTUS_MAC_ICON_DIR}/partial.plist"
        DEPENDS ${HEPHAESTUS_MAC_ICON_INPUTS}
        COMMENT "Compiling macOS app icon"
        VERBATIM
    )
    set_source_files_properties(${HEPHAESTUS_MAC_ICON_FILES} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources GENERATED TRUE)
    # One owner for the actool rule, so bundles that share the icon never run it in parallel.
    add_custom_target(HephaestusMacIcon DEPENDS ${HEPHAESTUS_MAC_ICON_FILES})
endif()

function(hephaestus_mac_bundle target_name bundle_name bundle_id)
    if(NOT APPLE OR NOT TARGET ${target_name})
        return()
    endif()
    target_sources(${target_name} PRIVATE ${HEPHAESTUS_MAC_ICON_FILES})
    add_dependencies(${target_name} HephaestusMacIcon)
    set_target_properties(${target_name} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacBundleInfo.plist.in"
        MACOSX_BUNDLE_ICON_FILE "MacOS-Icon"
        MACOSX_BUNDLE_BUNDLE_NAME "${bundle_name}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${bundle_id}"
        MACOSX_BUNDLE_INFO_STRING "${HEPHAESTUS_APP_DESCRIPTION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${HEPHAESTUS_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${HEPHAESTUS_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "${HEPHAESTUS_COPYRIGHT}"
    )
endfunction()

hephaestus_mac_bundle(HephaestusEngine "${HEPHAESTUS_APP_NAME}" "${HEPHAESTUS_DOMAIN}")
hephaestus_mac_bundle(HephaestusLauncher "Hephaestus Launcher" "${HEPHAESTUS_DOMAIN}.launcher")
hephaestus_mac_bundle(HephaestusGame "HephaestusGame" "${HEPHAESTUS_DOMAIN}.game")

if(APPLE AND TARGET HephaestusGame)
    # The packager copies HephaestusGame.app into each game, so it carries its own libraries.
    find_file(HEPHAESTUS_MOLTENVK_DYLIB libMoltenVK.dylib HINTS "$ENV{VULKAN_SDK}/lib" /usr/local/lib)
    find_file(HEPHAESTUS_MOLTENVK_ICD MoltenVK_icd.json
        HINTS "$ENV{VULKAN_SDK}/share/vulkan/icd.d" /usr/local/share/vulkan/icd.d)
    if(HEPHAESTUS_MOLTENVK_DYLIB AND HEPHAESTUS_MOLTENVK_ICD)
        set_target_properties(HephaestusGame PROPERTIES
            BUILD_WITH_INSTALL_RPATH ON
            INSTALL_RPATH "@executable_path/../Frameworks"
        )
        add_custom_command(TARGET HephaestusGame POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DAPP=$<TARGET_BUNDLE_DIR:HephaestusGame>"
                "-DEXE=$<TARGET_FILE:HephaestusGame>"
                "-DVULKAN=${Vulkan_LIBRARY}"
                "-DMOLTENVK=${HEPHAESTUS_MOLTENVK_DYLIB}"
                "-DMOLTENVK_ICD=${HEPHAESTUS_MOLTENVK_ICD}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BundleMacRuntime.cmake"
            COMMENT "Bundling Lua, Vulkan and MoltenVK into HephaestusGame.app"
            VERBATIM
        )
    else()
        message(WARNING "MoltenVK not found; HephaestusGame.app will need the Vulkan SDK installed to run")
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Install Targets for Distribution
# ─────────────────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

# Install editor
if(TARGET HephaestusEngine)
    install(TARGETS HephaestusEngine
        RUNTIME DESTINATION .
        BUNDLE DESTINATION .
    )
endif()

# Install game runtime
if(TARGET HephaestusGame)
    install(TARGETS HephaestusGame
        RUNTIME DESTINATION .
        BUNDLE DESTINATION .
    )
endif()
install(TARGETS HephaestusBuildCli RUNTIME DESTINATION tools)

# Install launcher
if(TARGET HephaestusLauncher)
    install(TARGETS HephaestusLauncher
        RUNTIME DESTINATION .
        BUNDLE DESTINATION .
    )
endif()

# Install shaders
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shaders/
    DESTINATION shaders
    FILES_MATCHING PATTERN "*.spv"
)
install(DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/shaders/"
    DESTINATION shaders
    OPTIONAL
    FILES_MATCHING PATTERN "*.spv"
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/.engine/meshes/
    DESTINATION .engine/meshes
    OPTIONAL
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/hdris/
    DESTINATION .engine/hdris
    OPTIONAL
)

install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md
    ${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
    DESTINATION .
)

# Install assets
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/
    DESTINATION assets
)

# Install scripts
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/scripts/
    DESTINATION scripts
    FILES_MATCHING PATTERN "*.lua"
)

# Install editor resources (icons, fonts) - editor only
if(TARGET HephaestusEngine)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/editor/
        DESTINATION editor
    )
endif()

# Install Lua DLL if present
if(LUA_FOUND AND DEFINED LUA_DLL_PATH AND EXISTS "${LUA_DLL_PATH}")
    install(FILES "${LUA_DLL_PATH}" DESTINATION .)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Material compatibility tests
# ─────────────────────────────────────────────────────────────────────────────
option(HEPHAESTUS_BUILD_COMPAT_TESTS "Build material compatibility tests" ON)
if(HEPHAESTUS_BUILD_COMPAT_TESTS)
    include(CTest)

    add_executable(MaterialCompatibilityTests
        engine/Tests/MaterialCompatibilityTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(MaterialCompatibilityTests PRIVATE HephaestusEditorCore)

    target_include_directories(MaterialCompatibilityTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )

    add_test(
        NAME MaterialCompatibilityRoundtrip
        COMMAND MaterialCompatibilityTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(PrefabSystemTests
        engine/Tests/PrefabSystemTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(PrefabSystemTests PRIVATE HephaestusRuntime)

    target_include_directories(PrefabSystemTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )

    add_test(
        NAME PrefabSystemWorkflow
        COMMAND PrefabSystemTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(SceneAuthoringTests
        engine/Tests/SceneAuthoringTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(SceneAuthoringTests PRIVATE HephaestusEditorCore)
    target_include_directories(SceneAuthoringTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    add_test(
        NAME SceneAuthoringWorkflow
        COMMAND SceneAuthoringTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(RuntimeSceneLifecycleTests
        engine/Tests/RuntimeSceneLifecycleTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(RuntimeSceneLifecycleTests PRIVATE HephaestusEditorCore)
    target_include_directories(RuntimeSceneLifecycleTests PRIVATE
        engine
        extern
        extern/entt/single_include
        extern/VMA
    )
    target_link_libraries(RuntimeSceneLifecycleTests PRIVATE HephaestusAutomationCore Vulkan::Vulkan)
    add_test(
        NAME RuntimeSceneLifecycle
        COMMAND RuntimeSceneLifecycleTests
    )

    add_executable(SkyIrradianceTests
        engine/Tests/SkyIrradianceTests.cpp
    )
    target_link_libraries(SkyIrradianceTests PRIVATE HephaestusRuntime)
    target_include_directories(SkyIrradianceTests PRIVATE engine extern)
    add_test(
        NAME SkyIrradiance
        COMMAND SkyIrradianceTests
    )

    add_executable(IniFileTests
        engine/Tests/IniFileTests.cpp
    )
    target_link_libraries(IniFileTests PRIVATE HephaestusRuntime)
    target_include_directories(IniFileTests PRIVATE engine)
    add_test(
        NAME IniFile
        COMMAND IniFileTests
    )

    add_executable(TextureDownscaleTests
        engine/Tests/TextureDownscaleTests.cpp
    )
    target_link_libraries(TextureDownscaleTests PRIVATE HephaestusRuntime)
    target_include_directories(TextureDownscaleTests PRIVATE engine)
    add_test(
        NAME TextureDownscale
        COMMAND TextureDownscaleTests
    )

    add_executable(AppPathsTests
        engine/Tests/AppPathsTests.cpp
    )
    target_link_libraries(AppPathsTests PRIVATE HephaestusRuntime)
    target_include_directories(AppPathsTests PRIVATE engine)
    add_test(
        NAME AppPaths
        COMMAND AppPathsTests
    )

    add_executable(SwapchainFormatTests
        engine/Tests/SwapchainFormatTests.cpp
    )
    target_link_libraries(SwapchainFormatTests PRIVATE HephaestusRuntime)
    target_include_directories(SwapchainFormatTests PRIVATE engine extern)
    add_test(
        NAME SwapchainFormat
        COMMAND SwapchainFormatTests
    )

    add_executable(NavMeshTests
        engine/Tests/NavMeshTests.cpp
    )
    target_link_libraries(NavMeshTests PRIVATE HephaestusRuntime)
    target_include_directories(NavMeshTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    add_test(
        NAME NavMesh
        COMMAND NavMeshTests
    )

    add_executable(ParticleSimulationTests
        engine/Tests/ParticleSimulationTests.cpp
    )
    target_link_libraries(ParticleSimulationTests PRIVATE HephaestusRuntime)
    target_include_directories(ParticleSimulationTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    add_test(
        NAME ParticleSimulation
        COMMAND ParticleSimulationTests
    )

    add_executable(AudioEngineTests
        engine/Tests/AudioEngineTests.cpp
    )
    target_link_libraries(AudioEngineTests PRIVATE HephaestusRuntime)
    target_include_directories(AudioEngineTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    add_test(
        NAME AudioEngine
        COMMAND AudioEngineTests
    )

    add_executable(ActiveCameraSyncTests
        engine/Tests/ActiveCameraSyncTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(ActiveCameraSyncTests PRIVATE HephaestusRuntime)
    target_include_directories(ActiveCameraSyncTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    add_test(
        NAME ActiveCameraSync
        COMMAND ActiveCameraSyncTests
    )

    add_executable(InspectorEditTests
        engine/Tests/InspectorEditTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(InspectorEditTests PRIVATE HephaestusEditorCore)
    target_include_directories(InspectorEditTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    add_test(
        NAME InspectorEditWorkflow
        COMMAND InspectorEditTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(AssetPathPolicyTests
        engine/Tests/AssetPathPolicyTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )

    target_include_directories(AssetPathPolicyTests PRIVATE
        engine
        extern
    )

    add_test(
        NAME AssetPathPolicy
        COMMAND AssetPathPolicyTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(ModelImporterTests
        engine/Tests/ModelImporterTests.cpp
    )
    target_link_libraries(ModelImporterTests PRIVATE HephaestusRuntime)
    target_include_directories(ModelImporterTests PRIVATE
        engine
        extern
        extern/VMA
    )
    target_link_libraries(ModelImporterTests PRIVATE Vulkan::Vulkan)
    hephaestus_target_ufbx(ModelImporterTests STATIC)
    add_test(
        NAME ModelImporter
        COMMAND ModelImporterTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(InputSystemTests
        engine/Tests/InputSystemTests.cpp
    )
    target_link_libraries(InputSystemTests PRIVATE HephaestusInputCore)
    target_include_directories(InputSystemTests PRIVATE engine extern)
    add_test(
        NAME InputSystem
        COMMAND InputSystemTests
    )

    add_executable(InputOverrideTests
        engine/Tests/InputOverrideTests.cpp
    )
    target_link_libraries(InputOverrideTests PRIVATE HephaestusInputCore)
    target_include_directories(InputOverrideTests PRIVATE engine extern)
    add_test(
        NAME InputOverride
        COMMAND InputOverrideTests
    )

    add_executable(HttpTransportSmokeTests
        engine/Tests/HttpTransportSmokeTests.cpp
    )
    target_link_libraries(HttpTransportSmokeTests PRIVATE HephaestusMcpCore)
    target_include_directories(HttpTransportSmokeTests PRIVATE extern)
    add_test(
        NAME HttpTransportSmoke
        COMMAND HttpTransportSmokeTests
    )

    add_executable(AutomationServiceTests
        engine/Tests/AutomationServiceTests.cpp
    )
    target_link_libraries(AutomationServiceTests PRIVATE HephaestusAutomationCore)
    target_include_directories(AutomationServiceTests PRIVATE engine extern)
    add_test(
        NAME AutomationService
        COMMAND AutomationServiceTests
    )

    add_executable(McpProtocolTests
        engine/Tests/McpProtocolTests.cpp
    )
    target_link_libraries(McpProtocolTests PRIVATE HephaestusMcpCore)
    target_include_directories(McpProtocolTests PRIVATE engine extern)
    add_test(
        NAME McpProtocol
        COMMAND McpProtocolTests
    )

    add_executable(AutomationPlayTests
        engine/Tests/AutomationPlayTests.cpp
    )
    target_link_libraries(AutomationPlayTests PRIVATE HephaestusAutomationCore)
    target_include_directories(AutomationPlayTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    add_test(
        NAME AutomationPlay
        COMMAND AutomationPlayTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(AutomationLogTests
        engine/Tests/AutomationLogTests.cpp
        engine/Core/RuntimeBootstrap.cpp
    )
    target_link_libraries(AutomationLogTests PRIVATE HephaestusRuntime)
    target_link_libraries(AutomationLogTests PRIVATE HephaestusAutomationCore)
    target_include_directories(AutomationLogTests PRIVATE
        engine
        extern
        extern/entt/single_include
        "${CMAKE_BINARY_DIR}/generated"
    )
    add_test(
        NAME AutomationLog
        COMMAND AutomationLogTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(AutomationBuildTests
        engine/Tests/AutomationBuildTests.cpp
    )
    target_link_libraries(AutomationBuildTests PRIVATE HephaestusAutomationCore)
    target_include_directories(AutomationBuildTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    add_test(
        NAME AutomationBuild
        COMMAND AutomationBuildTests
    )

    add_executable(AutomationSceneTests
        engine/Tests/AutomationSceneTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(AutomationSceneTests PRIVATE HephaestusEditorCore)
    target_link_libraries(AutomationSceneTests PRIVATE HephaestusAutomationCore)
    target_include_directories(AutomationSceneTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    add_test(
        NAME AutomationScene
        COMMAND AutomationSceneTests ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_executable(AutomationEditorViewportTests
        engine/Tests/AutomationEditorViewportTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(AutomationEditorViewportTests PRIVATE HephaestusRuntime)
    target_link_libraries(AutomationEditorViewportTests PRIVATE HephaestusAutomationCore)
    target_include_directories(AutomationEditorViewportTests PRIVATE engine extern extern/entt/single_include)
    add_test(NAME AutomationEditorViewport COMMAND AutomationEditorViewportTests)

    add_executable(AutomationUITests
        engine/Tests/AutomationUITests.cpp
    )
    target_link_libraries(AutomationUITests PRIVATE HephaestusRuntime)
    target_link_libraries(AutomationUITests PRIVATE HephaestusAutomationCore)
    target_include_directories(AutomationUITests PRIVATE engine extern extern/entt/single_include)
    add_test(NAME AutomationUI COMMAND AutomationUITests)

    add_executable(AutomationPhysicsTests
        engine/Tests/AutomationPhysicsTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(AutomationPhysicsTests PRIVATE HephaestusRuntime)
    target_link_libraries(AutomationPhysicsTests PRIVATE HephaestusAutomationCore Jolt ufbx Vulkan::Vulkan)
    target_include_directories(AutomationPhysicsTests PRIVATE
        engine
        extern
        extern/entt/single_include
        extern/JoltPhysics
        extern/JoltPhysics/Build
        extern/VMA
        "${CMAKE_BINARY_DIR}/generated"
    )
    add_test(NAME AutomationPhysics COMMAND AutomationPhysicsTests)

    add_executable(PhysicsSettlingReproTests
        engine/Tests/PhysicsSettlingReproTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(PhysicsSettlingReproTests PRIVATE HephaestusRuntime)
    target_link_libraries(PhysicsSettlingReproTests PRIVATE Jolt ufbx Vulkan::Vulkan)
    target_include_directories(PhysicsSettlingReproTests PRIVATE
        engine
        extern
        extern/entt/single_include
        extern/JoltPhysics
        extern/JoltPhysics/Build
        extern/VMA
        "${CMAKE_BINARY_DIR}/generated"
    )
    add_test(NAME PhysicsSettlingRepro COMMAND PhysicsSettlingReproTests)
    set_tests_properties(PhysicsSettlingRepro PROPERTIES TIMEOUT 1800)

    add_executable(PhysicsFoundationTests
        engine/Tests/PhysicsFoundationTests.cpp
    )
    target_link_libraries(PhysicsFoundationTests PRIVATE HephaestusRuntime)
    target_include_directories(PhysicsFoundationTests PRIVATE
        engine
        "${CMAKE_BINARY_DIR}/generated"
    )
    add_test(NAME PhysicsFoundation COMMAND PhysicsFoundationTests)

    add_executable(JoltPhysicsIntegrationTests
        engine/Tests/JoltPhysicsIntegrationTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(JoltPhysicsIntegrationTests PRIVATE HephaestusRuntime)
    target_link_libraries(JoltPhysicsIntegrationTests PRIVATE Jolt ufbx Vulkan::Vulkan)
    target_include_directories(JoltPhysicsIntegrationTests PRIVATE
        engine
        extern
        extern/entt/single_include
        extern/JoltPhysics
        extern/JoltPhysics/Build
        extern/VMA
        "${CMAKE_BINARY_DIR}/generated"
    )
    add_test(NAME JoltPhysicsIntegration COMMAND JoltPhysicsIntegrationTests)

    add_executable(CharacterControllerMathTests
        engine/Tests/CharacterControllerMathTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(CharacterControllerMathTests PRIVATE HephaestusRuntime)
    target_include_directories(CharacterControllerMathTests PRIVATE
        engine
        extern
        extern/entt/single_include
        "${CMAKE_BINARY_DIR}/generated"
    )
    add_test(NAME CharacterControllerMath COMMAND CharacterControllerMathTests)

    add_executable(ProbeVolumeTests
        engine/Tests/ProbeVolumeTests.cpp
    )
    target_link_libraries(ProbeVolumeTests PRIVATE HephaestusRuntime)
    target_include_directories(ProbeVolumeTests PRIVATE engine extern extern/entt/single_include)
    add_test(NAME ProbeVolume COMMAND ProbeVolumeTests)

    add_executable(LightShadowSettingsTests
        engine/Tests/LightShadowSettingsTests.cpp
    )
    target_link_libraries(LightShadowSettingsTests PRIVATE HephaestusRuntime)
    target_include_directories(LightShadowSettingsTests PRIVATE engine extern extern/entt/single_include)
    add_test(NAME LightShadowSettings COMMAND LightShadowSettingsTests)

    add_executable(PostProcessSettingsTests
        engine/Tests/PostProcessSettingsTests.cpp
    )
    target_link_libraries(PostProcessSettingsTests PRIVATE HephaestusRuntime)
    target_include_directories(PostProcessSettingsTests PRIVATE engine extern extern/entt/single_include)
    add_test(NAME PostProcessSettings COMMAND PostProcessSettingsTests)

    add_executable(EditorIconsTests
        engine/Tests/EditorIconsTests.cpp
    )
    target_link_libraries(EditorIconsTests PRIVATE HephaestusRuntime)
    target_include_directories(EditorIconsTests PRIVATE engine extern extern/entt/single_include)
    add_test(NAME EditorIcons COMMAND EditorIconsTests)

    add_executable(ProbeBakeTests
        engine/Tests/ProbeBakeTests.cpp
    )
    target_link_libraries(ProbeBakeTests PRIVATE HephaestusRuntime)
    target_include_directories(ProbeBakeTests PRIVATE engine extern extern/entt/single_include)
    add_test(NAME ProbeBake COMMAND ProbeBakeTests)

    add_executable(CharacterControllerJoltTests
        engine/Tests/CharacterControllerJoltTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(CharacterControllerJoltTests PRIVATE HephaestusRuntime)
    target_link_libraries(CharacterControllerJoltTests PRIVATE Jolt ufbx Vulkan::Vulkan)
    target_include_directories(CharacterControllerJoltTests PRIVATE
        engine
        extern
        extern/entt/single_include
        extern/JoltPhysics
        extern/JoltPhysics/Build
        extern/VMA
        "${CMAKE_BINARY_DIR}/generated"
    )
    add_test(NAME CharacterControllerJolt COMMAND CharacterControllerJoltTests)

    add_executable(AutomationRuntimeCaptureTests
        engine/Tests/AutomationRuntimeCaptureTests.cpp
    )
    target_link_libraries(AutomationRuntimeCaptureTests PRIVATE HephaestusAutomationCore)
    target_include_directories(AutomationRuntimeCaptureTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    add_test(NAME AutomationRuntimeCapture COMMAND AutomationRuntimeCaptureTests)

    add_executable(AutomationAssetTests
        engine/Tests/AutomationAssetTests.cpp
        engine/Assets/AssetReferenceIndex.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(AutomationAssetTests PRIVATE HephaestusEditorCore)
    target_link_libraries(AutomationAssetTests PRIVATE HephaestusAutomationCore Vulkan::Vulkan)
    target_include_directories(AutomationAssetTests PRIVATE
        engine
        extern
        extern/VMA
        extern/entt/single_include
    )
    hephaestus_target_ufbx(AutomationAssetTests STATIC)
    add_test(
        NAME AutomationAsset
        COMMAND AutomationAssetTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(AssetThumbnailRendererTests
        engine/Tests/AssetThumbnailRendererTests.cpp
    )
    target_link_libraries(AssetThumbnailRendererTests PRIVATE HephaestusEditorCore Vulkan::Vulkan)
    target_include_directories(AssetThumbnailRendererTests PRIVATE
        engine
        extern
        extern/VMA
        extern/entt/single_include
    )
    hephaestus_target_ufbx(AssetThumbnailRendererTests STATIC)
    add_test(NAME AssetThumbnailRenderer COMMAND AssetThumbnailRendererTests)

    add_executable(MaterialNodeSupportTests
        engine/Tests/MaterialNodeSupportTests.cpp
    )
    target_link_libraries(MaterialNodeSupportTests PRIVATE HephaestusEditorCore Vulkan::Vulkan)
    target_include_directories(MaterialNodeSupportTests PRIVATE
        engine
        extern
        extern/VMA
        extern/entt/single_include
    )
    hephaestus_target_ufbx(MaterialNodeSupportTests STATIC)
    add_test(NAME MaterialNodeSupport COMMAND MaterialNodeSupportTests)

    add_executable(FileWatcherTests
        engine/Tests/FileWatcherTests.cpp
    )
    target_link_libraries(FileWatcherTests PRIVATE HephaestusEditorCore Vulkan::Vulkan)
    target_include_directories(FileWatcherTests PRIVATE
        engine
        extern
        extern/VMA
        extern/entt/single_include
    )
    hephaestus_target_ufbx(FileWatcherTests STATIC)
    add_test(NAME FileWatcher COMMAND FileWatcherTests)

    add_executable(AutomationMaterialTests
        engine/Tests/AutomationMaterialTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(AutomationMaterialTests PRIVATE HephaestusEditorCore)
    target_link_libraries(AutomationMaterialTests PRIVATE HephaestusAutomationCore)
    target_include_directories(AutomationMaterialTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    add_test(
        NAME AutomationMaterial
        COMMAND AutomationMaterialTests
    )

    add_executable(AutomationCharacterTests
        engine/Tests/AutomationCharacterTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(AutomationCharacterTests PRIVATE HephaestusRuntime)
    target_link_libraries(AutomationCharacterTests PRIVATE HephaestusAutomationCore)
    target_include_directories(AutomationCharacterTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    hephaestus_target_ufbx(AutomationCharacterTests SKELETAL)
    add_test(
        NAME AutomationCharacter
        COMMAND AutomationCharacterTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(AutomationScriptTests
        engine/Tests/AutomationScriptTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(AutomationScriptTests PRIVATE HephaestusRuntime)
    target_link_libraries(AutomationScriptTests PRIVATE HephaestusAutomationCore)
    target_include_directories(AutomationScriptTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    add_test(
        NAME AutomationScript
        COMMAND AutomationScriptTests
    )

    add_executable(LuaExposeScannerTests
        engine/Tests/LuaExposeScannerTests.cpp
    )
    target_link_libraries(LuaExposeScannerTests PRIVATE HephaestusRuntime)
    target_include_directories(LuaExposeScannerTests PRIVATE engine extern)
    add_test(
        NAME LuaExposeScanner
        COMMAND LuaExposeScannerTests
    )

    add_executable(LuaScriptVariableTests
        engine/Tests/LuaScriptVariableTests.cpp
    )
    target_link_libraries(LuaScriptVariableTests PRIVATE HephaestusRuntime)
    target_include_directories(LuaScriptVariableTests PRIVATE engine extern)
    add_test(
        NAME LuaScriptVariables
        COMMAND LuaScriptVariableTests
    )

    if(LUA_FOUND)
        add_executable(LuaDebuggerTests
            engine/Tests/LuaDebuggerTests.cpp
        )
        target_link_libraries(LuaDebuggerTests PRIVATE HephaestusRuntime)
        target_include_directories(LuaDebuggerTests PRIVATE engine extern)
        add_test(NAME LuaDebugger COMMAND LuaDebuggerTests)

        add_executable(CharacterLuaTests
            engine/Tests/CharacterLuaTests.cpp
            engine/Assets/AssetPathPolicy.cpp
        )
        target_link_libraries(CharacterLuaTests PRIVATE HephaestusRuntime)
        target_include_directories(CharacterLuaTests PRIVATE
            engine
            extern
            extern/entt/single_include
            "${CMAKE_BINARY_DIR}/generated"
            ${LUA_INCLUDE_DIR}
        )
        if(TARGET Lua::Lua)
            target_link_libraries(CharacterLuaTests PRIVATE Lua::Lua glfw HephaestusInputCore)
        else()
            target_link_libraries(CharacterLuaTests PRIVATE ${LUA_LIBRARIES} glfw HephaestusInputCore)
        endif()
        hephaestus_target_ufbx(CharacterLuaTests SKELETAL)
        add_test(
            NAME CharacterLua
            COMMAND CharacterLuaTests
        )
        if(DEFINED LUA_DLL_PATH AND EXISTS "${LUA_DLL_PATH}")
            get_filename_component(LUA_DLL_NAME "${LUA_DLL_PATH}" NAME)
            add_custom_command(TARGET CharacterLuaTests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LUA_DLL_PATH}"
                    "$<TARGET_FILE_DIR:CharacterLuaTests>/${LUA_DLL_NAME}"
            )
        endif()

        add_executable(PhysicsLuaTests
            engine/Tests/PhysicsLuaTests.cpp
            engine/Assets/AssetPathPolicy.cpp
        )
        target_link_libraries(PhysicsLuaTests PRIVATE HephaestusRuntime)
        target_include_directories(PhysicsLuaTests PRIVATE
            engine
            extern
            extern/entt/single_include
            extern/JoltPhysics
            extern/JoltPhysics/Build
            extern/VMA
            "${CMAKE_BINARY_DIR}/generated"
            ${LUA_INCLUDE_DIR}
        )
        if(TARGET Lua::Lua)
            target_link_libraries(PhysicsLuaTests PRIVATE Lua::Lua glfw HephaestusInputCore Jolt Vulkan::Vulkan)
        else()
            target_link_libraries(PhysicsLuaTests PRIVATE ${LUA_LIBRARIES} glfw HephaestusInputCore Jolt Vulkan::Vulkan)
        endif()
        hephaestus_target_ufbx(PhysicsLuaTests SKELETAL)
        add_test(NAME PhysicsLua COMMAND PhysicsLuaTests)
        if(DEFINED LUA_DLL_PATH AND EXISTS "${LUA_DLL_PATH}")
            get_filename_component(PHYSICS_LUA_DLL_NAME "${LUA_DLL_PATH}" NAME)
            add_custom_command(TARGET PhysicsLuaTests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LUA_DLL_PATH}"
                    "$<TARGET_FILE_DIR:PhysicsLuaTests>/${PHYSICS_LUA_DLL_NAME}"
            )
        endif()

        # A scripting language written in C, linked in and also loaded as a module.
        add_library(ScriptLanguageTestPlugin SHARED engine/Tests/ScriptLanguageTestPlugin.c)
        add_executable(ScriptLanguagePluginTests
            engine/Tests/ScriptLanguagePluginTests.cpp
            engine/Tests/ScriptLanguageTestPlugin.c
        )
        target_link_libraries(ScriptLanguagePluginTests PRIVATE HephaestusRuntime)
        add_dependencies(ScriptLanguagePluginTests ScriptLanguageTestPlugin)
        add_test(NAME ScriptLanguagePlugins
            COMMAND ScriptLanguagePluginTests $<TARGET_FILE:ScriptLanguageTestPlugin>)
        if(DEFINED LUA_DLL_PATH AND EXISTS "${LUA_DLL_PATH}")
            add_custom_command(TARGET ScriptLanguagePluginTests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LUA_DLL_PATH}"
                    "$<TARGET_FILE_DIR:ScriptLanguagePluginTests>/${PHYSICS_LUA_DLL_NAME}"
            )
        endif()

        add_executable(SimulationStepTests engine/Tests/SimulationStepTests.cpp)
        target_link_libraries(SimulationStepTests PRIVATE HephaestusRuntime)
        add_test(NAME SimulationStep COMMAND SimulationStepTests)
        if(DEFINED LUA_DLL_PATH AND EXISTS "${LUA_DLL_PATH}")
            add_custom_command(TARGET SimulationStepTests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LUA_DLL_PATH}"
                    "$<TARGET_FILE_DIR:SimulationStepTests>/${PHYSICS_LUA_DLL_NAME}"
            )
        endif()
    endif()

    add_executable(AssetReferenceIndexTests
        engine/Tests/AssetReferenceIndexTests.cpp
        engine/Assets/AssetReferenceIndex.cpp
        engine/Assets/AssetPathPolicy.cpp
    )

    target_include_directories(AssetReferenceIndexTests PRIVATE
        engine
        extern
    )

    add_test(
        NAME AssetReferenceIndex
        COMMAND AssetReferenceIndexTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(CharacterAssetTests
        engine/Tests/CharacterAssetTests.cpp
        engine/Assets/AssetReferenceIndex.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(CharacterAssetTests PRIVATE HephaestusRuntime)
    target_include_directories(CharacterAssetTests PRIVATE
        engine
        extern
    )
    add_test(
        NAME CharacterAsset
        COMMAND CharacterAssetTests
    )

    add_executable(SkeletalImportTests
        engine/Tests/SkeletalImportTests.cpp
    )
    target_link_libraries(SkeletalImportTests PRIVATE HephaestusRuntime)
    target_include_directories(SkeletalImportTests PRIVATE
        engine
        extern
    )
    hephaestus_target_ufbx(SkeletalImportTests SKELETAL)
    add_test(
        NAME SkeletalImport
        COMMAND SkeletalImportTests
    )

    add_executable(SkinnedMeshTests
        engine/Tests/SkinnedMeshTests.cpp
    )
    target_link_libraries(SkinnedMeshTests PRIVATE HephaestusRuntime)
    target_include_directories(SkinnedMeshTests PRIVATE
        engine
        extern
        extern/VMA
    )
    target_link_libraries(SkinnedMeshTests PRIVATE Vulkan::Vulkan)
    hephaestus_target_ufbx(SkinnedMeshTests SKELETAL)
    add_test(
        NAME SkinnedMesh
        COMMAND SkinnedMeshTests
    )

    add_executable(CharacterPlaybackTests
        engine/Tests/CharacterPlaybackTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(CharacterPlaybackTests PRIVATE HephaestusRuntime)
    target_include_directories(CharacterPlaybackTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    hephaestus_target_ufbx(CharacterPlaybackTests SKELETAL)
    add_test(
        NAME CharacterPlayback
        COMMAND CharacterPlaybackTests
    )

    add_executable(CharacterWorkflowTests
        engine/Tests/CharacterWorkflowTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(CharacterWorkflowTests PRIVATE HephaestusRuntime)
    target_include_directories(CharacterWorkflowTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    hephaestus_target_ufbx(CharacterWorkflowTests SKELETAL)
    add_test(
        NAME CharacterWorkflow
        COMMAND CharacterWorkflowTests
    )

    add_executable(CharacterEditorTests
        engine/Tests/CharacterEditorTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(CharacterEditorTests PRIVATE HephaestusEditorCore)
    target_include_directories(CharacterEditorTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    hephaestus_target_ufbx(CharacterEditorTests SKELETAL)
    add_test(
        NAME CharacterEditor
        COMMAND CharacterEditorTests
    )

    add_executable(FbxImportTests
        engine/Tests/FbxImportTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(FbxImportTests PRIVATE HephaestusRuntime)
    target_include_directories(FbxImportTests PRIVATE
        engine
        extern
        extern/VMA
        extern/entt/single_include
    )
    target_link_libraries(FbxImportTests PRIVATE Vulkan::Vulkan)
    hephaestus_target_ufbx(FbxImportTests STATIC SKELETAL)
    add_test(
        NAME FbxImport
        COMMAND FbxImportTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(AssetOperationTransactionTests
        engine/Tests/AssetOperationTransactionTests.cpp
        engine/Assets/AssetReferenceIndex.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_link_libraries(AssetOperationTransactionTests PRIVATE HephaestusEditorCore)

    target_include_directories(AssetOperationTransactionTests PRIVATE
        engine
        extern
    )

    add_test(
        NAME AssetOperationTransaction
        COMMAND AssetOperationTransactionTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(AssetIntakeTests
        engine/Tests/AssetIntakeTests.cpp
    )
    target_link_libraries(AssetIntakeTests PRIVATE HephaestusEditorCore)
    target_include_directories(AssetIntakeTests PRIVATE
        engine
        extern
    )
    add_test(
        NAME AssetIntake
        COMMAND AssetIntakeTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(RecoveryStoreTests
        engine/Tests/RecoveryStoreTests.cpp
    )
    target_link_libraries(RecoveryStoreTests PRIVATE HephaestusEditorCore)

    target_include_directories(RecoveryStoreTests PRIVATE
        engine
        extern
    )

    add_test(
        NAME RecoveryStore
        COMMAND RecoveryStoreTests
    )

    if(TARGET HephaestusToolsCommon)
        add_executable(ProjectCreationTests
            engine/Tests/ProjectCreationTests.cpp
        )
        target_link_libraries(ProjectCreationTests PRIVATE HephaestusToolsCommon HephaestusRuntime Qt6::Core)
        target_include_directories(ProjectCreationTests PRIVATE
            engine
            "${CMAKE_BINARY_DIR}/generated"
        )
        add_test(
            NAME ProjectCreationLayout
            COMMAND ProjectCreationTests
        )
        if(TARGET Qt6::Core)
            set(_heph_project_creation_path "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
            if(TARGET HephaestusEngine)
                string(APPEND _heph_project_creation_path
                    ";PATH=path_list_prepend:$<TARGET_FILE_DIR:HephaestusEngine>")
            endif()
            set_tests_properties(ProjectCreationLayout PROPERTIES
                ENVIRONMENT_MODIFICATION "${_heph_project_creation_path}")

            add_executable(LauncherModelTests
                engine/Tests/LauncherModelTests.cpp
                engine/UI/Launcher/ProjectCardModel.h
                engine/UI/Launcher/ProjectCardModel.cpp
            )
            set_target_properties(LauncherModelTests PROPERTIES AUTOMOC ON)
            target_link_libraries(LauncherModelTests PRIVATE Qt6::Core)
            target_include_directories(LauncherModelTests PRIVATE engine)
            add_test(NAME LauncherModel COMMAND LauncherModelTests)
            set_tests_properties(LauncherModel PROPERTIES
                ENVIRONMENT_MODIFICATION "${_heph_project_creation_path}")
        endif()
    endif()

    add_executable(EditorLogicSmokeTests
        engine/Tests/EditorLogicSmokeTests.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
    target_include_directories(EditorLogicSmokeTests PRIVATE
        engine
        extern
    )
    add_test(
        NAME EditorLogicSmoke
        COMMAND EditorLogicSmokeTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(ProjectValidationTests
        engine/Tests/ProjectValidationTests.cpp
    )
    target_link_libraries(ProjectValidationTests PRIVATE HephaestusBuildCore)
    target_include_directories(ProjectValidationTests PRIVATE engine extern)
    add_test(
        NAME ProjectValidation
        COMMAND ProjectValidationTests
    )

    add_executable(RuntimeLocatorTests
        engine/Tests/RuntimeLocatorTests.cpp
    )
    target_link_libraries(RuntimeLocatorTests PRIVATE HephaestusBuildCore)
    target_include_directories(RuntimeLocatorTests PRIVATE engine extern)
    add_test(
        NAME RuntimeLocator
        COMMAND RuntimeLocatorTests
    )

    add_executable(BuildServiceTests
        engine/Tests/BuildServiceTests.cpp
    )
    target_link_libraries(BuildServiceTests PRIVATE HephaestusBuildCore)
    target_include_directories(BuildServiceTests PRIVATE engine extern)
    add_test(
        NAME BuildService
        COMMAND BuildServiceTests
    )

    add_executable(PackageLayoutTests
        engine/Tests/PackageLayoutTests.cpp
    )
    target_link_libraries(PackageLayoutTests PRIVATE HephaestusBuildCore)
    target_include_directories(PackageLayoutTests PRIVATE engine extern)
    add_test(
        NAME PackageLayout
        COMMAND PackageLayoutTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(PluginSystemTests engine/Tests/PluginSystemTests.cpp)
    target_link_libraries(PluginSystemTests PRIVATE HephaestusPluginCore)
    target_include_directories(PluginSystemTests PRIVATE engine extern)
    add_test(NAME PluginSystem COMMAND PluginSystemTests)

    add_executable(FPSInputMergeTests engine/Tests/FPSInputMergeTests.cpp)
    target_include_directories(FPSInputMergeTests PRIVATE
        engine
        extern
        Plugins/HephaestusFPS/Source
    )
    add_test(NAME FPSInputMerge COMMAND FPSInputMergeTests)

    add_executable(FPSPluginLayoutTests engine/Tests/FPSPluginLayoutTests.cpp)
    target_link_libraries(FPSPluginLayoutTests PRIVATE HephaestusPluginCore)
    target_include_directories(FPSPluginLayoutTests PRIVATE engine extern)
    add_test(
        NAME FPSPluginLayout
        COMMAND FPSPluginLayoutTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(LUA_FOUND)
        add_executable(FPSLogicTests engine/Tests/FPSLogicTests.cpp)
        target_include_directories(FPSLogicTests PRIVATE ${LUA_INCLUDE_DIR})
        target_compile_definitions(FPSLogicTests PRIVATE LUA_AVAILABLE=1)
        if(TARGET Lua::Lua)
            target_link_libraries(FPSLogicTests PRIVATE Lua::Lua)
        else()
            target_link_libraries(FPSLogicTests PRIVATE ${LUA_LIBRARIES})
        endif()
        add_test(
            NAME FPSLogic
            COMMAND FPSLogicTests ${CMAKE_CURRENT_SOURCE_DIR}
        )
        if(DEFINED LUA_DLL_PATH AND EXISTS "${LUA_DLL_PATH}")
            get_filename_component(FPS_LOGIC_LUA_DLL_NAME "${LUA_DLL_PATH}" NAME)
            add_custom_command(TARGET FPSLogicTests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LUA_DLL_PATH}"
                    "$<TARGET_FILE_DIR:FPSLogicTests>/${FPS_LOGIC_LUA_DLL_NAME}"
            )
        endif()
    endif()

    add_executable(HUIRendererTests
        engine/Tests/HUIRendererTests.cpp
    )
    target_link_libraries(HUIRendererTests PRIVATE HephaestusRuntime)
    target_include_directories(HUIRendererTests PRIVATE engine extern)
    add_test(
        NAME HUIRendererWorkflow
        COMMAND HUIRendererTests ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# Optional narrow Qt Test harness (off by default — requires Qt6 Test module)
option(HEPHAESTUS_BUILD_QT_TESTS "Build optional Qt Test editor harness" OFF)
if(HEPHAESTUS_BUILD_QT_TESTS AND HEPHAESTUS_USE_QT AND Qt6_FOUND)
    find_package(Qt6 COMPONENTS Test QUIET)
    if(Qt6Test_FOUND)
        add_executable(EditorQtSmokeTests
            engine/Tests/EditorQtSmokeTests.cpp
            engine/UI/QtEditor/PropertyEditors.cpp
            engine/UI/QtEditor/PropertyEditors.h
            engine/UI/QtEditor/EditorCommandSearch.cpp
            engine/UI/QtEditor/EditorConsolePanel.cpp
            engine/UI/QtEditor/CodeEditorPanel.cpp
            engine/UI/QtEditor/ProjectSearchPanel.cpp
            engine/UI/QtEditor/EditorTheme.cpp
            engine/UI/QtEditor/QtViewportWindow.cpp
            engine/UI/QtEditor/Resources/EditorResources.qrc
        )
        set_target_properties(EditorQtSmokeTests PROPERTIES AUTOMOC ON AUTORCC ON)
        target_compile_definitions(EditorQtSmokeTests PRIVATE HEPHAESTUS_EDITOR_QT_UI=1)
        target_link_libraries(EditorQtSmokeTests PRIVATE Qt6::Test Qt6::Widgets Qt6::Svg HephaestusEditorCore)
        target_include_directories(EditorQtSmokeTests PRIVATE engine)
        add_test(NAME EditorQtSmoke COMMAND EditorQtSmokeTests)
        # Qt's own bin folder first so Qt finds its plugins; offscreen so no window or error dialog can appear.
        set_tests_properties(EditorQtSmoke PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/$<CONFIG>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen")
    else()
        message(STATUS "HEPHAESTUS_BUILD_QT_TESTS=ON but Qt6 Test not found; skipping")
    endif()
endif()

# Keep test runners out of the editor/runtime output directory.  This preserves
# a clean Release root for the three user-facing executables while leaving the
# tests directly buildable and discoverable by CTest.
get_property(HEPHAESTUS_DIRECTORY_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(HEPHAESTUS_TARGET IN LISTS HEPHAESTUS_DIRECTORY_TARGETS)
    if(HEPHAESTUS_TARGET MATCHES "Test(s|Plugin)$")
        set_target_properties(${HEPHAESTUS_TARGET} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/tests"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release/tests"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug/tests"
        )
    endif()
endforeach()
unset(HEPHAESTUS_DIRECTORY_TARGETS)
unset(HEPHAESTUS_TARGET)

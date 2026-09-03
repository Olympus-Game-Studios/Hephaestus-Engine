cmake_minimum_required(VERSION 3.24)
project(HephaestusEngine LANGUAGES C CXX)

# ─────────────────────────────────────────────────────────────────────────────
# Application Details  (single source of truth — edit these, not C++ files)
# ─────────────────────────────────────────────────────────────────────────────
set(HEPHAESTUS_VERSION_MAJOR 2026)
set(HEPHAESTUS_VERSION_MINOR 9)
set(HEPHAESTUS_VERSION_PATCH 0)
set(HEPHAESTUS_VERSION "${HEPHAESTUS_VERSION_MAJOR}.${HEPHAESTUS_VERSION_MINOR}.${HEPHAESTUS_VERSION_PATCH}")

set(HEPHAESTUS_APP_NAME        "Hephaestus Engine")
set(HEPHAESTUS_APP_DESCRIPTION "A 3D game engine built with C++20, Vulkan, Qt, and Lua.")
set(HEPHAESTUS_ORGANIZATION    "Hephaestus")
set(HEPHAESTUS_DOMAIN          "dev.olympusgames.hephaestus")
set(HEPHAESTUS_WEBSITE         "https://hepheastus.olympusgames.dev")
set(HEPHAESTUS_COPYRIGHT       "Copyright (c) 2026 Hephaestus Contributors")

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
endif()

# Jolt Physics Engine
# ─────────────────────────────────────────────────────────────────────────────
# Build Configuration Options
# ─────────────────────────────────────────────────────────────────────────────
option(HEPHAESTUS_BUILD_EDITOR "Build the editor (HephaestusEngine.exe)" ON)
option(HEPHAESTUS_BUILD_GAME "Build the standalone game runtime (HephaestusGame.exe)" ON)
option(HEPHAESTUS_GAME_SCRIPTING "Enable Lua scripting in game runtime" ON)
option(HEPHAESTUS_USE_QT "Enable Qt-based editor UI" ON)
option(HEPHAESTUS_USE_SHARED_ASSETS "Use shared runtime/editor/launcher asset folders" ON)

# Qt (Windows) - use C:\Qt as a hint for discovery
set(HEPHAESTUS_QT_ROOT "C:/Qt" CACHE PATH "Root path to Qt installation")
if(HEPHAESTUS_USE_QT)
    # Try to auto-locate Qt6Config.cmake under common Qt layout
    # IMPORTANT: Must use MSVC builds when compiling with Visual Studio
    if(EXISTS "${HEPHAESTUS_QT_ROOT}" AND NOT Qt6_DIR)
        # First try to find MSVC builds specifically
        file(GLOB _qt6_msvc_candidates
            "${HEPHAESTUS_QT_ROOT}/6.*/msvc*/lib/cmake/Qt6/Qt6Config.cmake"
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
# Disable static MSVC runtime to avoid linker conflicts
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Use dynamic MSVC runtime" FORCE)
add_subdirectory(extern/JoltPhysics/Build)

# Shared gameplay input core used by the Qt editor, packaged runtime, launcher
# settings UI, and focused headless tests.
add_library(HephaestusInputCore STATIC
    engine/Input/InputMap.cpp
    engine/Input/InputSystem.cpp
)
target_include_directories(HephaestusInputCore PUBLIC engine extern extern/glfw/include)

# ─────────────────────────────────────────────────────────────────────────────
# Shared Engine Sources (used by both Editor and Game)
# ─────────────────────────────────────────────────────────────────────────────
    set(ENGINE_CORE_SOURCES
    engine/Audio/AudioEngine.cpp
    engine/Assets/ModelImporter.cpp
    engine/Assets/FbxDocument.cpp
    engine/Assets/FbxStaticImporter.cpp
    engine/Assets/FbxSkeletalImporter.cpp
    engine/Assets/AssetDatabase.cpp
    engine/Assets/AssetImporters.cpp
    engine/Assets/AssetManager.cpp
    engine/Assets/AssetPathPolicy.cpp
    engine/Assets/AssetReferenceIndex.cpp
    engine/Assets/CharacterAsset.cpp
    engine/Assets/CharacterRuntime.cpp
    engine/Assets/CharacterRuntimeGpu.cpp
    engine/Assets/CharacterImport.cpp
    engine/Assets/SkeletalModelImporter.cpp
    engine/Animation/Skeleton.cpp
    engine/Animation/AnimationClip.cpp
    engine/Animation/CharacterPlayback.cpp
    engine/Assets/OpenDocumentRegistry.cpp
    engine/Assets/AssetOperationService.cpp
    engine/Assets/DerivedDataCache.cpp
    engine/Assets/EditorAssetPipeline.cpp
    engine/Assets/EditorViewState.cpp
    engine/Assets/FileWatcher.cpp
    engine/Assets/RecoveryStore.cpp
    engine/Assets/ReimportManager.cpp
    engine/Core/EditorContext.cpp
    engine/Core/EditorCameraController.cpp
    engine/Core/ProjectContext.cpp
    engine/Core/PlayModeManager.cpp
    engine/Core/EngineLog.cpp
    engine/Gfx/VulkanDevice.cpp
    engine/Gfx/VulkanSwapchain.cpp
    engine/Gfx/VulkanPipeline.cpp
    engine/Gfx/VulkanTexture.cpp
    engine/Gfx/EngineFrameRenderer.cpp
    engine/Physics/MeshCollisionSystem.cpp
    engine/Physics/JoltPhysicsEngine.cpp
    engine/Physics/JoltPhysicsSystem.cpp
    engine/Physics/CharacterMotor.cpp
    engine/Scene/Scene.cpp
    engine/Scene/SceneAuthoring.cpp
    engine/Scene/Entity.cpp
    engine/Scene/Json.cpp
    engine/Scene/PrefabSerializer.cpp
    engine/Scene/PrefabInstantiator.cpp
    engine/Scene/PrefabComponentIO.cpp
    engine/Scene/PrefabOverrideSystem.cpp
    engine/Scene/SceneSerializer.cpp
    engine/Scripting/ScriptBase.cpp
    engine/Scripting/ScriptEngine.cpp
    engine/Scripting/LuaScriptEngine.cpp
    engine/Scripting/LuaExposeScanner.cpp
    engine/Scripting/LuaScriptVariableReconciler.cpp
    engine/UI/HephaestusUI/HUIAsset.cpp
    engine/UI/HephaestusUI/UIButton.cpp
    engine/UI/HephaestusUI/UICanvas.cpp
    engine/UI/HephaestusUI/UIContext.cpp
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
)

# Editor-only sources
set(EDITOR_ONLY_SOURCES
    engine/Core/main.cpp
    engine/Gfx/VulkanGizmo.cpp
    engine/Gfx/VulkanLightGizmo.cpp
    engine/UI/EditorDocument.cpp
    engine/UI/CharacterDocument.cpp
    engine/UI/MaterialEditor/MaterialGraph.cpp
    engine/UI/MaterialEditor/MaterialNodeDefinitions.cpp
    engine/UI/MaterialEditor/MaterialGraphEvaluator.cpp
    engine/UI/MaterialEditor/MaterialShaderGenerator.cpp
    engine/UI/MaterialEditor/MaterialAssetSerializer.cpp
    engine/UI/MaterialEditor/TextureDiscovery.cpp
)

# Game runtime-only sources
set(GAME_RUNTIME_SOURCES
    engine/Game/GameMain.cpp
    engine/Core/RuntimeBootstrap.cpp
)

# Shared non-Qt project validation and packaging pipeline used by the editor
# and headless CLI. Keep compiler/process orchestration out of Qt UI code.
add_library(HephaestusBuildCore STATIC
    engine/Build/BuildManifest.cpp
    engine/Build/BuildService.cpp
    engine/Build/PackageStager.cpp
    engine/Build/ProjectValidator.cpp
    engine/Build/RuntimeLocator.cpp
    engine/Assets/AssetPathPolicy.cpp
    engine/Assets/AssetReferenceIndex.cpp
)
target_include_directories(HephaestusBuildCore PUBLIC engine extern)

add_executable(HephaestusBuildCli engine/BuildCli.cpp)
target_link_libraries(HephaestusBuildCli PRIVATE HephaestusBuildCore)
target_include_directories(HephaestusBuildCli PRIVATE engine extern "${CMAKE_BINARY_DIR}/generated")
set_target_properties(HephaestusBuildCli PROPERTIES OUTPUT_NAME "HephaestusBuild")

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
    engine/Audio/AudioEngine.cpp
    engine/Core/main.cpp
    engine/Core/EngineLog.cpp
    engine/Core/EditorContext.cpp
    engine/Core/EditorCameraController.cpp
    engine/Core/ProjectContext.cpp
    engine/Core/PlayModeManager.cpp
    engine/Assets/ModelImporter.cpp
    engine/Assets/FbxDocument.cpp
    engine/Assets/FbxStaticImporter.cpp
    engine/Assets/FbxSkeletalImporter.cpp
    engine/Assets/AssetDatabase.cpp
    engine/Assets/AssetImporters.cpp
    engine/Assets/AssetManager.cpp
    engine/Assets/OpenDocumentRegistry.cpp
    engine/Assets/CharacterAsset.cpp
    engine/Assets/CharacterRuntime.cpp
    engine/Assets/CharacterRuntimeGpu.cpp
    engine/Assets/CharacterImport.cpp
    engine/Assets/SkeletalModelImporter.cpp
    engine/Animation/Skeleton.cpp
    engine/Animation/AnimationClip.cpp
    engine/Animation/CharacterPlayback.cpp
    engine/Assets/AssetOperationService.cpp
    engine/Assets/DerivedDataCache.cpp
    engine/Assets/EditorAssetPipeline.cpp
    engine/Assets/FileWatcher.cpp
    engine/Assets/RecoveryStore.cpp
    engine/Assets/EditorViewState.cpp
    engine/Assets/ReimportManager.cpp
    engine/Gfx/VulkanDevice.cpp
    engine/Gfx/VulkanSwapchain.cpp
    engine/Gfx/VulkanPipeline.cpp
    engine/Gfx/VulkanTexture.cpp
    engine/Gfx/VulkanGizmo.cpp
    engine/Gfx/VulkanLightGizmo.cpp
    engine/Gfx/EngineFrameRenderer.cpp
    engine/Physics/MeshCollisionSystem.cpp
    engine/Physics/JoltPhysicsEngine.cpp
    engine/Physics/JoltPhysicsSystem.cpp
    engine/Physics/CharacterMotor.cpp
    engine/Scene/Scene.cpp
    engine/Scene/SceneAuthoring.cpp
    engine/Scene/Entity.cpp
    engine/Scene/Json.cpp
    engine/Scene/PrefabSerializer.cpp
    engine/Scene/PrefabInstantiator.cpp
    engine/Scene/PrefabComponentIO.cpp
    engine/Scene/PrefabOverrideSystem.cpp
    engine/Scene/SceneSerializer.cpp
    engine/Scripting/ScriptBase.cpp
    engine/Scripting/ScriptEngine.cpp
    engine/Scripting/LuaScriptEngine.cpp
    engine/Scripting/LuaExposeScanner.cpp
    engine/Scripting/LuaScriptVariableReconciler.cpp
    engine/UI/HephaestusUI/HUIAsset.cpp
    engine/UI/HephaestusUI/UIButton.cpp
    engine/UI/HephaestusUI/UICanvas.cpp
    engine/UI/HephaestusUI/UIContext.cpp
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

if (WIN32)
    set(APP_ICON_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/editor/Icons/logo")
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
elseif(APPLE)
    # macOS App Bundle setup — version/metadata sourced from CMake variables
    set_target_properties(HephaestusEngine PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "${HEPHAESTUS_APP_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${HEPHAESTUS_DOMAIN}"
        MACOSX_BUNDLE_INFO_STRING "${HEPHAESTUS_APP_DESCRIPTION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${HEPHAESTUS_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${HEPHAESTUS_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "${HEPHAESTUS_COPYRIGHT}"
    )
endif()

target_include_directories(HephaestusEngine PRIVATE engine "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(HephaestusEngine PRIVATE HephaestusBuildCore HephaestusInputCore ufbx)
if(LUA_FOUND)
    if(TARGET Lua::Lua)
        target_link_libraries(HephaestusEngine PRIVATE Vulkan::Vulkan glfw Jolt Lua::Lua)
    else()
        # Manual vcpkg Lua
        target_link_libraries(HephaestusEngine PRIVATE Vulkan::Vulkan glfw Jolt ${LUA_LIBRARIES})
    endif()
    target_include_directories(HephaestusEngine PRIVATE ${LUA_INCLUDE_DIR})
    target_compile_definitions(HephaestusEngine PRIVATE LUA_AVAILABLE=1)
    
    # Copy Lua DLL to output directory
    if(DEFINED LUA_DLL_PATH AND EXISTS "${LUA_DLL_PATH}")
        get_filename_component(LUA_DLL_NAME "${LUA_DLL_PATH}" NAME)
        add_custom_command(TARGET HephaestusEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LUA_DLL_PATH}"
                "$<TARGET_FILE_DIR:HephaestusEngine>/${LUA_DLL_NAME}"
            COMMENT "Copying ${LUA_DLL_NAME} to output directory"
        )
    endif()
else()
    target_link_libraries(HephaestusEngine PRIVATE Vulkan::Vulkan glfw Jolt)
    # Lua disabled - LuaScriptEngine will be a stub
    target_compile_definitions(HephaestusEngine PRIVATE LUA_AVAILABLE=0)
endif()
if(WIN32)
    target_link_libraries(HephaestusEngine PRIVATE winmm)
endif()
target_include_directories(HephaestusEngine PRIVATE
    engine
    extern
    extern/VMA
    extern/entt/single_include
    extern/JoltPhysics
    extern/JoltPhysics/Build
)


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
            engine/UI/QtEditor/EditorAssetServices.cpp
            engine/UI/QtEditor/EditorAssetServices.h
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
            engine/UI/QtEditor/PrefabHierarchyPanel.h
            engine/UI/QtEditor/PrefabInspectorPanel.cpp
            engine/UI/QtEditor/PrefabInspectorPanel.h
            engine/UI/QtEditor/PrefabInstanceBar.cpp
            engine/UI/QtEditor/PrefabInstanceBar.h
            engine/UI/Shared/SettingsWindow.cpp
            engine/UI/Shared/SettingsWindow.h
            engine/UI/Shared/InputMapEditorWidget.cpp
            engine/UI/Shared/InputMapEditorWidget.h
        )

        target_link_libraries(HephaestusEngine PRIVATE Qt6::Widgets)
        if(TARGET HephaestusToolsCommon)
            target_link_libraries(HephaestusEngine PRIVATE HephaestusToolsCommon)
        endif()
        target_compile_definitions(HephaestusEngine PRIVATE HEPHAESTUS_EDITOR_QT_UI=1)

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
                COMMENT "Copying Qt6 DLLs to output directory"
            )

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
    engine/UI/Shared/SettingsWindow.h
    engine/UI/Shared/SettingsWindow.cpp
    engine/UI/Shared/InputMapEditorWidget.h
    engine/UI/Shared/InputMapEditorWidget.cpp
)

if(NOT HEPHAESTUS_USE_SHARED_ASSETS)
    target_sources(HephaestusLauncher PRIVATE
        engine/UI/Launcher/launcher_fallback.qrc
    )
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
    HephaestusToolsCommon
    HephaestusInputCore
)

if(TARGET Qt6::Concurrent)
    target_link_libraries(HephaestusLauncher PRIVATE Qt6::Concurrent)
endif()

if(TARGET Qt6::Svg)
    target_link_libraries(HephaestusLauncher PRIVATE Qt6::Svg)
    target_compile_definitions(HephaestusLauncher PRIVATE HEPHAESTUS_QT_SVG_AVAILABLE=1)
else()
    target_compile_definitions(HephaestusLauncher PRIVATE HEPHAESTUS_QT_SVG_AVAILABLE=0)
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
    ${ENGINE_CORE_SOURCES}
    ${GAME_RUNTIME_SOURCES}
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

# Game runtime compile definitions
target_compile_definitions(HephaestusGame PRIVATE
    HEPHAESTUS_GAME_RUNTIME=1
)

if(HEPHAESTUS_GAME_SCRIPTING AND LUA_FOUND)
    target_compile_definitions(HephaestusGame PRIVATE 
        HEPHAESTUS_GAME_SCRIPTING=1
        LUA_AVAILABLE=1
    )
else()
    target_compile_definitions(HephaestusGame PRIVATE 
        HEPHAESTUS_GAME_SCRIPTING=0
        LUA_AVAILABLE=0
    )
endif()

target_include_directories(HephaestusGame PRIVATE
    engine
    extern
    extern/VMA
    extern/entt/single_include
    extern/JoltPhysics
    extern/JoltPhysics/Build
    "${CMAKE_BINARY_DIR}/generated"
)

# Link libraries for game runtime
set(GAME_LINK_LIBS Vulkan::Vulkan glfw Jolt)

if(HEPHAESTUS_GAME_SCRIPTING AND LUA_FOUND)
    if(TARGET Lua::Lua)
        list(APPEND GAME_LINK_LIBS Lua::Lua)
    else()
        list(APPEND GAME_LINK_LIBS ${LUA_LIBRARIES})
        target_include_directories(HephaestusGame PRIVATE ${LUA_INCLUDE_DIR})
    endif()
endif()

target_link_libraries(HephaestusGame PRIVATE ${GAME_LINK_LIBS} HephaestusInputCore ufbx)

# Windows: link dbghelp for crash handler
if(WIN32)
    target_link_libraries(HephaestusGame PRIVATE dbghelp)
    target_link_libraries(HephaestusGame PRIVATE winmm)
endif()

# Set output directory for game executable
set_target_properties(HephaestusGame PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
)

# Copy Lua DLL to game output directory if using scripting
if(HEPHAESTUS_GAME_SCRIPTING AND LUA_FOUND AND WIN32)
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
    )

    set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/$<CONFIG>/shaders)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/Release/shaders)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/Debug/shaders)

    set(SHADER_OUTPUTS)
    foreach(SRC ${SHADER_SOURCES})
        get_filename_component(FN ${SRC} NAME)
        set(OUT ${SHADER_OUTPUT_DIR}/${FN}.spv)
        add_custom_command(
            OUTPUT ${OUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
            COMMAND ${GLSLC_EXECUTABLE} ${SRC} -o ${OUT}
            DEPENDS ${SRC}
            COMMENT "Compiling ${FN} -> ${OUT}"
            VERBATIM
        )
        list(APPEND SHADER_OUTPUTS ${OUT})
    endforeach()

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

    if(HEPHAESTUS_USE_SHARED_ASSETS)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:${target_name}>/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/editor"
                "$<TARGET_FILE_DIR:${target_name}>/editor"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
                "$<TARGET_FILE_DIR:${target_name}>/scripts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/.engine/meshes"
                "$<TARGET_FILE_DIR:${target_name}>/.engine/meshes"
            COMMENT "Copying shared runtime/editor assets for ${target_name}"
        )
    else()
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:${target_name}>/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
                "$<TARGET_FILE_DIR:${target_name}>/scripts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/.engine/meshes"
                "$<TARGET_FILE_DIR:${target_name}>/.engine/meshes"
            COMMENT "Copying runtime assets for ${target_name}"
        )
    endif()
endfunction()

hephaestus_add_shared_assets_copy(HephaestusEngine)
hephaestus_add_shared_assets_copy(HephaestusGame)
hephaestus_add_shared_assets_copy(HephaestusLauncher)

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
install(TARGETS HephaestusBuildCli RUNTIME DESTINATION .)

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
if(EXISTS "$ENV{USERPROFILE}/vcpkg/installed/x64-windows/bin/lua.dll")
    install(FILES "$ENV{USERPROFILE}/vcpkg/installed/x64-windows/bin/lua.dll"
        DESTINATION .
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Material compatibility tests
# ─────────────────────────────────────────────────────────────────────────────
option(HEPHAESTUS_BUILD_COMPAT_TESTS "Build material compatibility tests" ON)
if(HEPHAESTUS_BUILD_COMPAT_TESTS)
    include(CTest)

    add_executable(MaterialCompatibilityTests
        engine/Tests/MaterialCompatibilityTests.cpp
        engine/UI/MaterialEditor/MaterialGraph.cpp
        engine/UI/MaterialEditor/MaterialNodeDefinitions.cpp
        engine/UI/MaterialEditor/MaterialGraphEvaluator.cpp
        engine/UI/MaterialEditor/MaterialShaderGenerator.cpp
        engine/UI/MaterialEditor/MaterialAssetSerializer.cpp
        engine/Assets/AssetPathPolicy.cpp
    )

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
        engine/Scene/Json.cpp
        engine/Scene/Entity.cpp
        engine/Scene/Scene.cpp
        engine/Scene/PrefabSerializer.cpp
        engine/Scene/PrefabInstantiator.cpp
        engine/Scene/PrefabComponentIO.cpp
        engine/Scene/PrefabOverrideSystem.cpp
        engine/Scene/SceneSerializer.cpp
        engine/Assets/AssetPathPolicy.cpp
        engine/Scripting/ScriptBase.cpp
    )

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
        engine/Assets/EditorViewState.cpp
        engine/Assets/AssetPathPolicy.cpp
        engine/Scene/Json.cpp
        engine/Scene/Entity.cpp
        engine/Scene/Scene.cpp
        engine/Scene/SceneAuthoring.cpp
        engine/Scene/PrefabSerializer.cpp
        engine/Scene/PrefabInstantiator.cpp
        engine/Scene/PrefabComponentIO.cpp
        engine/Scene/PrefabOverrideSystem.cpp
        engine/Scene/SceneSerializer.cpp
        engine/Scripting/ScriptBase.cpp
    )
    target_include_directories(SceneAuthoringTests PRIVATE
        engine
        extern
        extern/entt/single_include
    )
    add_test(
        NAME SceneAuthoringWorkflow
        COMMAND SceneAuthoringTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(InspectorEditTests
        engine/Tests/InspectorEditTests.cpp
        engine/UI/InspectorSelectionModel.cpp
        engine/UI/ComponentClipboard.cpp
        engine/Assets/AssetPathPolicy.cpp
        engine/Scene/Json.cpp
        engine/Scene/Entity.cpp
        engine/Scene/Scene.cpp
        engine/Scene/PrefabSerializer.cpp
        engine/Scene/PrefabInstantiator.cpp
        engine/Scene/PrefabComponentIO.cpp
        engine/Scene/PrefabOverrideSystem.cpp
        engine/Scene/SceneSerializer.cpp
        engine/Scripting/ScriptBase.cpp
    )
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
        engine/Assets/ModelImporter.cpp
    )
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

    add_executable(LuaExposeScannerTests
        engine/Tests/LuaExposeScannerTests.cpp
        engine/Scripting/LuaExposeScanner.cpp
    )
    target_include_directories(LuaExposeScannerTests PRIVATE engine extern)
    add_test(
        NAME LuaExposeScanner
        COMMAND LuaExposeScannerTests
    )

    add_executable(LuaScriptVariableTests
        engine/Tests/LuaScriptVariableTests.cpp
        engine/Scripting/LuaExposeScanner.cpp
        engine/Scripting/LuaScriptVariableReconciler.cpp
    )
    target_include_directories(LuaScriptVariableTests PRIVATE engine extern)
    add_test(
        NAME LuaScriptVariables
        COMMAND LuaScriptVariableTests
    )

    if(LUA_FOUND)
        add_executable(CharacterLuaTests
            engine/Tests/CharacterLuaTests.cpp
            engine/Scripting/LuaScriptEngine.cpp
            engine/Scripting/LuaExposeScanner.cpp
            engine/Scripting/LuaScriptVariableReconciler.cpp
            engine/Scripting/ScriptBase.cpp
            engine/Audio/AudioEngine.cpp
            engine/Assets/CharacterAsset.cpp
            engine/Assets/CharacterRuntime.cpp
            engine/Assets/SkeletalModelImporter.cpp
            engine/Assets/AssetPathPolicy.cpp
            engine/Animation/Skeleton.cpp
            engine/Animation/AnimationClip.cpp
            engine/Animation/CharacterPlayback.cpp
            engine/Scene/Json.cpp
            engine/Scene/Entity.cpp
            engine/Scene/Scene.cpp
            engine/Scene/PrefabSerializer.cpp
            engine/Scene/PrefabInstantiator.cpp
            engine/Scene/PrefabComponentIO.cpp
            engine/Scene/PrefabOverrideSystem.cpp
            engine/Scene/SceneSerializer.cpp
            engine/UI/HephaestusUI/HUIAsset.cpp
            engine/UI/HephaestusUI/UIButton.cpp
            engine/UI/HephaestusUI/UICanvas.cpp
            engine/UI/HephaestusUI/UIContext.cpp
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
        )
        target_include_directories(CharacterLuaTests PRIVATE
            engine
            extern
            extern/entt/single_include
            ${LUA_INCLUDE_DIR}
        )
        target_compile_definitions(CharacterLuaTests PRIVATE LUA_AVAILABLE=1)
        if(TARGET Lua::Lua)
            target_link_libraries(CharacterLuaTests PRIVATE Lua::Lua glfw HephaestusInputCore)
        else()
            target_link_libraries(CharacterLuaTests PRIVATE ${LUA_LIBRARIES} glfw HephaestusInputCore)
        endif()
        hephaestus_target_ufbx(CharacterLuaTests SKELETAL)
        if(WIN32)
            target_link_libraries(CharacterLuaTests PRIVATE winmm)
        endif()
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
        engine/Assets/CharacterAsset.cpp
        engine/Assets/AssetReferenceIndex.cpp
        engine/Assets/AssetPathPolicy.cpp
    )
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
        engine/Animation/Skeleton.cpp
        engine/Animation/AnimationClip.cpp
        engine/Assets/SkeletalModelImporter.cpp
    )
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
        engine/Animation/Skeleton.cpp
        engine/Animation/AnimationClip.cpp
        engine/Assets/SkeletalModelImporter.cpp
    )
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
        engine/Animation/Skeleton.cpp
        engine/Animation/AnimationClip.cpp
        engine/Animation/CharacterPlayback.cpp
        engine/Assets/SkeletalModelImporter.cpp
        engine/Assets/AssetPathPolicy.cpp
        engine/Scene/Json.cpp
        engine/Scene/Entity.cpp
        engine/Scene/Scene.cpp
        engine/Scene/PrefabSerializer.cpp
        engine/Scene/PrefabInstantiator.cpp
        engine/Scene/PrefabComponentIO.cpp
        engine/Scene/PrefabOverrideSystem.cpp
        engine/Scene/SceneSerializer.cpp
        engine/Scripting/ScriptBase.cpp
    )
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
        engine/Assets/CharacterAsset.cpp
        engine/Assets/CharacterRuntime.cpp
        engine/Assets/CharacterImport.cpp
        engine/Assets/SkeletalModelImporter.cpp
        engine/Assets/AssetPathPolicy.cpp
        engine/Assets/AssetManager.cpp
        engine/Animation/Skeleton.cpp
        engine/Animation/AnimationClip.cpp
        engine/Animation/CharacterPlayback.cpp
        engine/Scene/Json.cpp
        engine/Scene/Entity.cpp
        engine/Scene/Scene.cpp
        engine/Scene/PrefabSerializer.cpp
        engine/Scene/PrefabInstantiator.cpp
        engine/Scene/PrefabComponentIO.cpp
        engine/Scene/PrefabOverrideSystem.cpp
        engine/Scene/SceneSerializer.cpp
        engine/Scripting/ScriptBase.cpp
    )
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
        engine/Assets/CharacterAsset.cpp
        engine/Assets/CharacterRuntime.cpp
        engine/Assets/CharacterImport.cpp
        engine/Assets/SkeletalModelImporter.cpp
        engine/Assets/AssetPathPolicy.cpp
        engine/Animation/Skeleton.cpp
        engine/Animation/AnimationClip.cpp
        engine/Animation/CharacterPlayback.cpp
        engine/Scene/Json.cpp
        engine/Scene/Entity.cpp
        engine/Scene/Scene.cpp
        engine/Scene/PrefabSerializer.cpp
        engine/Scene/PrefabInstantiator.cpp
        engine/Scene/PrefabComponentIO.cpp
        engine/Scene/PrefabOverrideSystem.cpp
        engine/Scene/SceneSerializer.cpp
        engine/Scripting/ScriptBase.cpp
        engine/UI/EditorDocument.cpp
        engine/UI/CharacterDocument.cpp
    )
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
        engine/Assets/ModelImporter.cpp
        engine/Assets/CharacterAsset.cpp
        engine/Assets/CharacterImport.cpp
        engine/Assets/CharacterRuntime.cpp
        engine/Assets/SkeletalModelImporter.cpp
        engine/Assets/AssetPathPolicy.cpp
        engine/Assets/AssetManager.cpp
        engine/Animation/Skeleton.cpp
        engine/Animation/AnimationClip.cpp
        engine/Animation/CharacterPlayback.cpp
        engine/Scene/Json.cpp
        engine/Scene/Entity.cpp
        engine/Scene/Scene.cpp
        engine/Scene/PrefabSerializer.cpp
        engine/Scene/PrefabInstantiator.cpp
        engine/Scene/PrefabComponentIO.cpp
        engine/Scene/PrefabOverrideSystem.cpp
        engine/Scene/SceneSerializer.cpp
        engine/Scripting/ScriptBase.cpp
    )
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
        engine/Assets/AssetOperationService.cpp
        engine/Assets/OpenDocumentRegistry.cpp
        engine/Assets/AssetReferenceIndex.cpp
        engine/Assets/AssetPathPolicy.cpp
        engine/Assets/AssetManager.cpp
        engine/Assets/CharacterAsset.cpp
    )

    target_include_directories(AssetOperationTransactionTests PRIVATE
        engine
        extern
    )

    add_test(
        NAME AssetOperationTransaction
        COMMAND AssetOperationTransactionTests ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(RecoveryStoreTests
        engine/Tests/RecoveryStoreTests.cpp
        engine/Assets/RecoveryStore.cpp
    )

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
        target_link_libraries(ProjectCreationTests PRIVATE HephaestusToolsCommon Qt6::Core)
        target_include_directories(ProjectCreationTests PRIVATE
            engine
            "${CMAKE_BINARY_DIR}/generated"
        )
        add_test(
            NAME ProjectCreationLayout
            COMMAND ProjectCreationTests
        )
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
endif()

# Optional narrow Qt Test harness (off by default — requires Qt6 Test module)
option(HEPHAESTUS_BUILD_QT_TESTS "Build optional Qt Test editor harness" OFF)
if(HEPHAESTUS_BUILD_QT_TESTS AND HEPHAESTUS_USE_QT AND Qt6_FOUND)
    find_package(Qt6 COMPONENTS Test QUIET)
    if(Qt6Test_FOUND)
        add_executable(EditorQtSmokeTests
            engine/Tests/EditorQtSmokeTests.cpp
        )
        target_link_libraries(EditorQtSmokeTests PRIVATE Qt6::Test Qt6::Widgets)
        target_include_directories(EditorQtSmokeTests PRIVATE engine)
        add_test(NAME EditorQtSmoke COMMAND EditorQtSmokeTests)
    else()
        message(STATUS "HEPHAESTUS_BUILD_QT_TESTS=ON but Qt6 Test not found; skipping")
    endif()
endif()


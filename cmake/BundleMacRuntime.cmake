# Run with cmake -P after HephaestusGame.app links. Copies the player's non-system
# libraries (Lua, the Vulkan loader, MoltenVK) into the bundle so packaged games run
# on Macs without Homebrew or the Vulkan SDK.
#   APP          path to HephaestusGame.app
#   EXE          path to its executable
#   VULKAN       the Vulkan loader the player linked (@rpath, so not found by otool)
#   MOLTENVK     libMoltenVK.dylib
#   MOLTENVK_ICD the SDK's MoltenVK_icd.json

set(frameworks "${APP}/Contents/Frameworks")
file(MAKE_DIRECTORY "${frameworks}")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${ARGN} failed: ${error}")
    endif()
endfunction()

# Copies a library into Frameworks once, with its own dependencies, and sets
# bundled_name to its file name there.
function(bundle_library library)
    file(REAL_PATH "${library}" real)
    execute_process(COMMAND otool -D "${real}" OUTPUT_VARIABLE id OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE ".*\n" "" id "${id}") # last line; universal binaries print one per arch
    get_filename_component(name "${id}" NAME)
    get_property(bundled GLOBAL PROPERTY HEPHAESTUS_BUNDLED_LIBRARIES)
    if(NOT name IN_LIST bundled)
        set_property(GLOBAL APPEND PROPERTY HEPHAESTUS_BUNDLED_LIBRARIES "${name}")
        file(COPY_FILE "${real}" "${frameworks}/${name}")
        run(install_name_tool -id "@rpath/${name}" "${frameworks}/${name}")
        bundle_dependencies("${frameworks}/${name}")
        run(codesign --force --sign - "${frameworks}/${name}")
    endif()
    set(bundled_name "${name}" PARENT_SCOPE)
endfunction()

# Bundles the absolute non-system libraries a binary links, such as Homebrew's Lua,
# and points the binary at the bundled copies.
function(bundle_dependencies binary)
    execute_process(COMMAND otool -L "${binary}" OUTPUT_VARIABLE dependencies)
    string(REPLACE "\n" ";" dependencies "${dependencies}")
    foreach(line IN LISTS dependencies)
        if(line MATCHES "^[ \t]+(/[^ ]+) \\(")
            set(dependency "${CMAKE_MATCH_1}")
            # Frameworks such as Qt are deployed by macdeployqt, not here.
            if(NOT dependency MATCHES "^/(System|usr/lib)/" AND NOT dependency MATCHES "\\.framework/")
                bundle_library("${dependency}")
                run(install_name_tool -change "${dependency}" "@rpath/${bundled_name}" "${binary}")
            endif()
        endif()
    endforeach()
endfunction()

bundle_library("${VULKAN}")
bundle_library("${MOLTENVK}")
bundle_dependencies("${EXE}")

# The Vulkan loader looks for drivers in the bundle's Resources/vulkan/icd.d.
file(READ "${MOLTENVK_ICD}" icd)
string(REGEX REPLACE "\"library_path\"[ \t]*:[ \t]*\"[^\"]*\""
       "\"library_path\": \"../../../Frameworks/libMoltenVK.dylib\"" icd "${icd}")
file(WRITE "${APP}/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json" "${icd}")

run(codesign --force --sign - "${APP}")

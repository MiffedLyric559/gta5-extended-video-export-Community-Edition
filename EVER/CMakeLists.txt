set(PROJECT_NAME EVER)

################################################################################
# Resource fix
################################################################################
foreach(LANG  C CXX RC)
    set(CMAKE_${LANG}_STANDARD_INCLUDE_DIRECTORIES ${CUSTOM_INCLUDE_DIRECTORIES})
endforeach()

################################################################################
# Source groups
################################################################################
source_group("" FILES ${no_group_source_files})

# Core files
set(Core_Header_Files
        "src/core/script.h")

set(Core_Source_Files
        "src/core/dllmain.cpp"
        "src/core/script.cpp")

# Video encoding files
set(Video_Header_Files
        "src/video/EncoderSession.h"
        "src/video/OpenEXRExporter.h"
        "src/video/VideoFrameTypes.h")

set(Video_Source_Files
        "src/video/EncoderSession.cpp"
        "src/video/OpenEXRExporter.cpp"
        "src/video/VideoFrameTypes.cpp")

# Hooking files
set(Hooking_Header_Files
        "src/hooking/HookUtility.h"
        "src/hooking/MemberFunctionHook.h"
        "src/hooking/ImportExportHook.h"
        "src/hooking/X64Detour.h"
        "src/hooking/PatternScanner.h"
        "src/hooking/ScanPatterns.h"
        "src/hooking/HookDefinitions.h")

set(Hooking_Source_Files
        "src/hooking/PatternScanner.cpp")

# Config files
set(Config_Header_Files
        "src/config/ConfigConstants.h"
        "src/config/Manager.h")

set(Config_Source_Files
        "src/config/Manager.cpp")

# Utility files
set(Utils_Header_Files
        "src/utils/ConfigValueParser.h"
        "src/utils/IniConfigReader.h"
        "src/utils/JsonPresetReader.h"
        "src/utils/logger.h"
        "src/utils/SafeQueue.h"
        "src/utils/util.h")

set(Utils_Source_Files
        "src/utils/logger.cpp"
        "src/utils/util.cpp")

# Rendering files
set(Rendering_Header_Files
        "src/rendering/MFUtility.h")

# Include files
set(Include_Files
        "include/stdafx.h")

# Resource files
set(Resource_Files
        "resources/resource.rc")

# Combine all header files
set(Header_Files
        ${Core_Header_Files}
        ${Video_Header_Files}
        ${Hooking_Header_Files}
        ${Config_Header_Files}
        ${Utils_Header_Files}
        ${Rendering_Header_Files}
        ${Include_Files})

# Combine all source files
set(Source_Files
        ${Core_Source_Files}
        ${Video_Source_Files}
        ${Hooking_Source_Files}
        ${Config_Source_Files}
        ${Utils_Source_Files}
        ${Resource_Files})

set(VK_Source_Files
        "../voukoder/VoukoderTypeLib_i.c")

source_group("Core\\Header Files" FILES ${Core_Header_Files})
source_group("Core\\Source Files" FILES ${Core_Source_Files})
source_group("Video\\Header Files" FILES ${Video_Header_Files})
source_group("Video\\Source Files" FILES ${Video_Source_Files})
source_group("Hooking\\Header Files" FILES ${Hooking_Header_Files})
source_group("Hooking\\Source Files" FILES ${Hooking_Source_Files})
source_group("Config\\Header Files" FILES ${Config_Header_Files})
source_group("Config\\Source Files" FILES ${Config_Source_Files})
source_group("Utils\\Header Files" FILES ${Utils_Header_Files})
source_group("Utils\\Source Files" FILES ${Utils_Source_Files})
source_group("Rendering\\Header Files" FILES ${Rendering_Header_Files})
source_group("Include Files" FILES ${Include_Files})
source_group("Resource Files" FILES ${Resource_Files})
source_group("External\\Voukoder" FILES ${VK_Source_Files})

set(ALL_FILES
        ${no_group_source_files}
        ${Header_Files}
        ${Source_Files}
        ${VK_Source_Files}
        )
################################################################################
# Target
################################################################################
add_library(${PROJECT_NAME} SHARED ${ALL_FILES})

use_props(${PROJECT_NAME} "${CMAKE_CONFIGURATION_TYPES}" "${DEFAULT_CXX_PROPS}")
set(ROOT_NAMESPACE EVER)

set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_GLOBAL_KEYWORD "Win32Proj"
        )

################################################################################
# Target name
################################################################################
set_target_properties(${PROJECT_NAME} PROPERTIES
        TARGET_NAME_DEBUG "EVER"
        TARGET_NAME_MINSIZEREL "EVER"
        TARGET_NAME_RELEASE "EVER"
        TARGET_NAME_RELWITHDEBINFO "EVER"
        SUFFIX ".asi"
        )

# Build HLSL shaders
add_custom_target(shaders)

set(HLSL_SHADER_FILES "shaders/PSAccumulate.hlsl" "shaders/PSDivide.hlsl" "shaders/VSFullScreen.hlsl")

set_source_files_properties("shaders/VSFullScreen.hlsl" PROPERTIES ShaderType "vs")
set_source_files_properties("shaders/PSAccumulate.hlsl" PROPERTIES ShaderType "ps")
set_source_files_properties("shaders/PSDivide.hlsl" PROPERTIES ShaderType "ps")
set_source_files_properties(${HLSL_SHADER_FILES} PROPERTIES ShaderModel "4_0")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")

add_custom_command(TARGET shaders
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders")

foreach(FILE ${HLSL_SHADER_FILES})
  get_filename_component(FILE_WE ${FILE} NAME_WE)
  get_source_file_property(shadertype ${FILE} ShaderType)
  get_source_file_property(shadermodel ${FILE} ShaderModel)
  add_custom_command(TARGET shaders
                     COMMAND fxc.exe /nologo /Emain /T${shadertype}_${shadermodel} $<IF:$<CONFIG:DEBUG>,/Od,/O1> /Zi /Fh ${CMAKE_BINARY_DIR}/shaders/${FILE_WE}.h /Fd ${CMAKE_BINARY_DIR}/shaders/${FILE_WE}.pdb ${FILE}
                     MAIN_DEPENDENCY ${FILE}
                     COMMENT "HLSL ${FILE}"
                     WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                     VERBATIM)
endforeach(FILE)

add_dependencies(${PROJECT_NAME} shaders)
################################################################################
# Compile definitions
################################################################################
target_compile_definitions(${PROJECT_NAME} PRIVATE
        "$<$<CONFIG:Debug>:"
        "_DEBUG;"
        "_SCL_SECURE_NO_WARNINGS"
        ">"
        "$<$<CONFIG:MinSizeRel>:"
        "NDEBUG;"
        "_CRT_NONSTDC_NO_WARNINGS"
        ">"
        "$<$<CONFIG:Release>:"
        "NDEBUG;"
        "_CRT_NONSTDC_NO_WARNINGS"
        ">"
        "$<$<CONFIG:RelWithDebInfo>:"
        "_DEBUG;"
        "_SCL_SECURE_NO_WARNINGS"
        ">"
        "_WINDOWS;"
        "_USRDLL;"
        "EVER_EXPORTS;"
        "TARGET_NAME=\"${TARGET_NAME}\";"
        "_CRT_SECURE_NO_WARNINGS;"
        "_WIN32_WINNT=0x0601"
        )

################################################################################
# Compile and link options
################################################################################
target_link_libraries(${PROJECT_NAME} PRIVATE delayimp)
target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:MinSizeRel>:
        /Y-
        >
        $<$<CONFIG:Release>:
        /Y-
        >
        ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT};
        ${DEFAULT_CXX_EXCEPTION_HANDLING}
        )
target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:
        /DELAYLOAD:OpenEXR-3_1_d.dll;
        /DEBUG:FULL
        >
        $<$<CONFIG:MinSizeRel>:
        /DELAYLOAD:OpenEXR-3_1.dll;
        >
        $<$<CONFIG:Release>:
        /DELAYLOAD:OpenEXR-3_1.dll;
        /MAPINFO:LINES;
        "/MAP:$<TARGET_FILE_DIR:${PROJECT_NAME}>/EVER_$<CONFIG>.map"
        >
        $<$<CONFIG:RelWithDebInfo>:
        /DELAYLOAD:OpenEXR-3_1.dll;
        /DEBUG:FULL
        >
        /DELAYLOAD:PolyHook_2.dll;
        /SUBSYSTEM:WINDOWS
        )

################################################################################
# Pre build events
################################################################################
add_custom_command_if(
        TARGET ${PROJECT_NAME}
        PRE_BUILD
        COMMANDS
)
################################################################################
# Post build events
################################################################################
add_custom_command_if(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMANDS
        COMMAND $<CONFIG:Debug> xcopy /y /E "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/>deploy\\*" "$<SHELL_PATH:${OUTPUT_DIRECTORY}>"
        COMMAND $<CONFIG:Debug> if not exist "$<SHELL_PATH:${OUTPUT_DIRECTORY}/>EVER\\dlls" mkdir "$<SHELL_PATH:${OUTPUT_DIRECTORY}/>EVER\\dlls"
        COMMAND $<CONFIG:Debug> move /Y "$<SHELL_PATH:${OUTPUT_DIRECTORY}>\\*.dll" "$<SHELL_PATH:${OUTPUT_DIRECTORY}/>EVER\\dlls"
        COMMAND $<CONFIG:MinSizeRel> del /F /Q "$<SHELL_PATH:${OUTPUT_DIRECTORY}>EVER"
        COMMAND $<CONFIG:MinSizeRel> xcopy /y /E "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/>deploy\\*" "$<SHELL_PATH:${OUTPUT_DIRECTORY}>"
        COMMAND $<CONFIG:Release> del /F /Q "$<SHELL_PATH:${OUTPUT_DIRECTORY}>EVER"
        COMMAND $<CONFIG:Release> xcopy /y /E "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/>deploy\\*" "$<SHELL_PATH:${OUTPUT_DIRECTORY}>"
        COMMAND $<CONFIG:Release> if not exist "$<SHELL_PATH:${OUTPUT_DIRECTORY}/>EVER\\dlls" mkdir "$<SHELL_PATH:${OUTPUT_DIRECTORY}/>EVER\\dlls"
        COMMAND $<CONFIG:Release> move /Y "$<SHELL_PATH:${OUTPUT_DIRECTORY}>\\*.dll" "$<SHELL_PATH:${OUTPUT_DIRECTORY}/>EVER\\dlls"
        COMMAND $<CONFIG:RelWithDebInfo> xcopy /y /E "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/>deploy\\*" "$<SHELL_PATH:${OUTPUT_DIRECTORY}>"
)

################################################################################
# Dependencies
################################################################################

# Add project include directories
target_include_directories(${PROJECT_NAME} PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/video"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/hooking"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/config"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rendering"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources")

# Shaders
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}/shaders")

# Reshade headers
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../reshade/include")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../reshade/deps/imgui")

# ScriptHookV
find_library(SCRIPT_HOOK_V_LIBRARY ScriptHookV REQUIRED HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../ScriptHookV/lib")
target_link_libraries(${PROJECT_NAME} PRIVATE ${SCRIPT_HOOK_V_LIBRARY})

# inih
find_path(INIH_INCLUDE_DIR ini.h REQUIRED)
find_library(INIH_LIBRARY inih REQUIRED)
target_include_directories(${PROJECT_NAME} PRIVATE ${INIH_INCLUDE_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${INIH_INCLUDE_DIR}/cpp)
find_package(unofficial-inih CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE unofficial::inih::inireader)

# PolyHook2
find_package(PolyHook_2 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE PolyHook_2::PolyHook_2)

# OpenEXR
find_package(OpenEXR CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenEXR::OpenEXR)

# Voukoder
find_path(VOUKODER_INCLUDE_DIR VoukoderTypeLib_h.h REQUIRED HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../voukoder")
target_include_directories(${PROJECT_NAME} PRIVATE ${VOUKODER_INCLUDE_DIR})

# D3D11
target_link_libraries(${PROJECT_NAME} PRIVATE d3d11.lib)

# Media Foundation
target_link_libraries(${PROJECT_NAME} PRIVATE mfreadwrite.lib mfplat.lib mfuuid.lib)

# USER32
target_link_libraries(${PROJECT_NAME} PRIVATE user32.lib)
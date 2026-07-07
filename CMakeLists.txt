cmake_minimum_required(VERSION 3.20)

project(GARMIN_GI260 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(XPLANE_SDK_DIR "$ENV{XPLANE_SDK_DIR}" CACHE PATH "Path to the X-Plane SDK root directory")
if(NOT XPLANE_SDK_DIR)
    set(XPLANE_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SDK" CACHE PATH "Path to the X-Plane SDK root directory" FORCE)
endif()

if(NOT EXISTS "${XPLANE_SDK_DIR}/CHeaders/XPLM/XPLMPlugin.h")
    message(FATAL_ERROR
        "X-Plane SDK not found. Set XPLANE_SDK_DIR to the SDK root, for example "
        "-DXPLANE_SDK_DIR=C:/X-PlaneSDK or set the XPLANE_SDK_DIR environment variable.")
endif()

set(XPLANE_PLUGIN_ROOT_DIR "D:/SteamLibrary/steamapps/common/X-Plane 12/Resources/plugins/Garmin GI260 Popout" CACHE PATH "X-Plane plugin deployment directory")
set(XPLANE_PLUGIN_OUTPUT_DIR "${XPLANE_PLUGIN_ROOT_DIR}/64")

add_library(GARMIN_GI260 SHARED
    src/plugin.cpp
)

target_compile_definitions(GARMIN_GI260 PRIVATE
    IBM=1
    XPLM200=1
    XPLM210=1
    XPLM300=1
    XPLM301=1
    XPLM400=1
)

target_include_directories(GARMIN_GI260 PRIVATE
    "${XPLANE_SDK_DIR}/CHeaders/XPLM"
    "${XPLANE_SDK_DIR}/CHeaders/Widgets"
    "${XPLANE_SDK_DIR}/CHeaders/Wrappers"
)

target_link_libraries(GARMIN_GI260 PRIVATE
    "${XPLANE_SDK_DIR}/Libraries/Win/XPLM_64.lib"
    "${XPLANE_SDK_DIR}/Libraries/Win/XPWidgets_64.lib"
    opengl32.lib
    windowscodecs.lib
    ole32.lib
)

set_target_properties(GARMIN_GI260 PROPERTIES
    PREFIX ""
    SUFFIX ".xpl"
    OUTPUT_NAME "win"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${XPLANE_PLUGIN_OUTPUT_DIR}>"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${XPLANE_PLUGIN_OUTPUT_DIR}>"
    ARCHIVE_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/lib>"
    PDB_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/pdb>"
    COMPILE_PDB_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/pdb>"
)

if(MSVC)
    target_compile_options(GARMIN_GI260 PRIVATE /W4 /permissive-)
endif()

add_custom_command(TARGET GARMIN_GI260 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${XPLANE_PLUGIN_ROOT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${XPLANE_PLUGIN_ROOT_DIR}/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Beep.wav"
        "${XPLANE_PLUGIN_ROOT_DIR}/Resources/Beep.wav"
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
        "if (!(Test-Path -LiteralPath '${XPLANE_PLUGIN_ROOT_DIR}/config.ini')) { Copy-Item -LiteralPath '${CMAKE_CURRENT_SOURCE_DIR}/config.ini' -Destination '${XPLANE_PLUGIN_ROOT_DIR}/config.ini' }"
    VERBATIM
)

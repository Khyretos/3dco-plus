cmake_minimum_required(VERSION 3.15)
project(3dco+)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Suppress PkgConfig CMP0219 warning (backslash handling in custom commands)
if(POLICY CMP0219)
    cmake_policy(SET CMP0219 NEW)
endif()

option(BUILD_STATIC "Build with static linking (when possible)" OFF)

find_package(PkgConfig REQUIRED)

if(BUILD_STATIC)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(NOT APPLE)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    endif()
    # For ZLIB, explicitly prefer static library
    set(ZLIB_USE_STATIC_LIBS ON)
endif()

pkg_check_modules(ASSIMP REQUIRED assimp)
pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(SPDLOG REQUIRED spdlog)
pkg_check_modules(FMT REQUIRED fmt)
pkg_check_modules(SDL3 REQUIRED sdl3)
pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json)

find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)

if(NOT APPLE)
    find_package(Threads REQUIRED)
    set(THREAD_LIBRARY Threads::Threads)
else()
    if(NOT TARGET Threads::Threads)
        add_library(Threads::Threads INTERFACE IMPORTED)
    endif()
    set(THREAD_LIBRARY "")
endif()

# ------------------------------------------------------------------
# Collect all source files (main src + only needed ImGui sources)
# ------------------------------------------------------------------
# Main sources from src/
file(GLOB_RECURSE SRC_FILES src/*.cpp src/*.c)

# ImGui core and backends – we need only GLFW and OpenGL3.
# Explicitly list them to avoid pulling in Allegro, DX, Vulkan, etc.
set(IMGUI_SRC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)
list(APPEND SRC_FILES ${IMGUI_SRC})

# Add ImGuiFileBrowser source if any
if(IMGUI_FILE_BROWSER_SRC)
    list(APPEND SRC_FILES ${IMGUI_FILE_BROWSER_SRC})
endif()

# Exclude any backup folders if present (from any location)
list(FILTER SRC_FILES EXCLUDE REGEX ".*/backup/.*")

# ------------------------------------------------------------------
# Generate models_zip_data.h from assets/models folder
# ------------------------------------------------------------------
set(GEN_SCRIPT "${CMAKE_SOURCE_DIR}/tools/generate_models_zip.py")
set(GEN_HEADER "${CMAKE_SOURCE_DIR}/src/models_zip_data.h")
set(MODELS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/assets/models")

# Always depend on the script itself, plus the models folder if it exists.
set(MODELS_DEPENDS "${GEN_SCRIPT}")
if(EXISTS "${MODELS_SOURCE_DIR}")
    list(APPEND MODELS_DEPENDS "${MODELS_SOURCE_DIR}")
else()
    message(WARNING "assets/models not found — building without an embedded "
                    "default model library. See README's 'Building' section.")
endif()

add_custom_command(
    OUTPUT ${GEN_HEADER}
    COMMAND python3 ${GEN_SCRIPT}
    DEPENDS ${MODELS_DEPENDS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating models_zip_data.h from assets/models"
)

# Mark header as generated
set_source_files_properties(${GEN_HEADER} PROPERTIES GENERATED TRUE)

# ------------------------------------------------------------------
# Generate gamecontrollerdb_data.h from assets/gamecontrollerdb.txt
# ------------------------------------------------------------------
set(DB_SCRIPT "${CMAKE_SOURCE_DIR}/tools/generate_db_header.py")
set(DB_HEADER "${CMAKE_SOURCE_DIR}/src/gamecontrollerdb_data.h")
set(DB_SOURCE "${CMAKE_SOURCE_DIR}/assets/gamecontrollerdb.txt")

add_custom_command(
    OUTPUT ${DB_HEADER}
    COMMAND python3 ${DB_SCRIPT}
    DEPENDS ${DB_SOURCE} ${DB_SCRIPT}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating gamecontrollerdb_data.h from assets/gamecontrollerdb.txt"
)

# Mark header as generated
set_source_files_properties(${DB_HEADER} PROPERTIES GENERATED TRUE)

# ------------------------------------------------------------------
# Generate icon_data.h from assets/icon.png
# ------------------------------------------------------------------
set(ICON_SCRIPT "${CMAKE_SOURCE_DIR}/tools/generate_icon_header.py")
set(ICON_HEADER "${CMAKE_SOURCE_DIR}/src/icon_data.h")
set(ICON_SOURCE "${CMAKE_SOURCE_DIR}/assets/icon.png")

add_custom_command(
    OUTPUT ${ICON_HEADER}
    COMMAND python3 ${ICON_SCRIPT}
    DEPENDS ${ICON_SOURCE} ${ICON_SCRIPT}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating icon_data.h from assets/icon.png"
)

set_source_files_properties(${ICON_HEADER} PROPERTIES GENERATED TRUE)

# Generate shaders_data.h
set(SHADER_SCRIPT "${CMAKE_SOURCE_DIR}/tools/generate_shaders_header.py")
set(SHADER_HEADER "${CMAKE_SOURCE_DIR}/src/shaders_data.h")
set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/assets/shaders")

add_custom_command(
    OUTPUT ${SHADER_HEADER}
    COMMAND python3 ${SHADER_SCRIPT}
    DEPENDS ${SHADER_SOURCE_DIR} ${SHADER_SCRIPT}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating shaders_data.h from assets/shaders"
)
set_source_files_properties(${SHADER_HEADER} PROPERTIES GENERATED TRUE)


# ============================================================
# ImGuiFileBrowser – locate the header locally
# ============================================================
set(IMGUI_FILE_BROWSER_INCLUDE_DIR "")
set(IMGUI_FILE_BROWSER_SRC "")

# Check both possible locations and casing variations
foreach(IMGUI_FILE_BROWSER_PATH
    "${CMAKE_SOURCE_DIR}/third_party/imgui"
    "${CMAKE_SOURCE_DIR}/third_party/include"
)
    if(EXISTS "${IMGUI_FILE_BROWSER_PATH}/imfilebrowser.h" OR EXISTS "${IMGUI_FILE_BROWSER_PATH}/ImGuiFileBrowser.h")
        set(IMGUI_FILE_BROWSER_INCLUDE_DIR "${IMGUI_FILE_BROWSER_PATH}")
        break()
    endif()
endforeach()

if(IMGUI_FILE_BROWSER_INCLUDE_DIR)
    message(STATUS "Found ImGuiFileBrowser at: ${IMGUI_FILE_BROWSER_INCLUDE_DIR}")
    # If a source file exists, add it
    if(EXISTS "${IMGUI_FILE_BROWSER_INCLUDE_DIR}/imfilebrowser.cpp")
        set(IMGUI_FILE_BROWSER_SRC "${IMGUI_FILE_BROWSER_INCLUDE_DIR}/imfilebrowser.cpp")
    elseif(EXISTS "${IMGUI_FILE_BROWSER_INCLUDE_DIR}/ImGuiFileBrowser.cpp")
        set(IMGUI_FILE_BROWSER_SRC "${IMGUI_FILE_BROWSER_INCLUDE_DIR}/ImGuiFileBrowser.cpp")
    endif()
else()
    message(FATAL_ERROR "Could not find ImGuiFileBrowser.h in third_party/imgui or third_party/include. Please place it there manually.")
endif()

# ------------------------------------------------------------------
# Executable – uses the collected sources + generated headers
#
# WIN32 here means "build as a GUI-subsystem app" (no console window)
# on Windows specifically - CMake ignores this keyword entirely on
# Linux/macOS, so it's safe to leave unconditional rather than
# wrapping it in if(WIN32). Previously the Windows build always
# opened a console window alongside the app (the default for a plain
# `int main()` executable), which duplicated the in-app log window/
# log file for no real benefit and just added visual clutter -
# CMake still generates the right WinMain-calling-main() entry point
# shim automatically, so no code changes were needed for this.
# ------------------------------------------------------------------
add_executable(3dco+ WIN32 ${SRC_FILES} ${GEN_HEADER} ${DB_HEADER} ${ICON_HEADER} ${SHADER_HEADER})

# Include directories
target_include_directories(3dco+ PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/third_party/include"
    "${CMAKE_SOURCE_DIR}/third_party/imgui"          # for public headers
    "${CMAKE_SOURCE_DIR}/third_party/imgui/backends" # for backend headers (imgui_impl_glfw.h etc.)
    "${IMGUI_FILE_BROWSER_INCLUDE_DIR}"
)

# ------------------------------------------------------------------
# IMPORTANT: pkg-config modules can carry required preprocessor
# defines in their "CFLAGS_OTHER" output (e.g. spdlog.pc emits
# -DSPDLOG_FMT_EXTERNAL when spdlog was built against an external
# fmt instead of its bundled copy). These must be forwarded to the
# actual compile command.
# ------------------------------------------------------------------
target_compile_options(3dco+ PRIVATE
    ${ASSIMP_CFLAGS_OTHER}
    ${GLFW_CFLAGS_OTHER}
    ${SPDLOG_CFLAGS_OTHER}
    ${FMT_CFLAGS_OTHER}
    ${SDL3_CFLAGS_OTHER}
    ${NLOHMANN_JSON_CFLAGS_OTHER}
)

target_compile_options(3dco+ PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_SOURCE_DIR}/src/preinclude.h>
)

if(APPLE)
    target_compile_options(3dco+ PRIVATE -pthread)
endif()

target_include_directories(3dco+ PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/include
    ${CMAKE_SOURCE_DIR}/third_party/imgui
    ${CMAKE_SOURCE_DIR}/third_party/imgui/backends
    ${GLFW_INCLUDE_DIRS}
    ${SDL3_INCLUDE_DIRS}
    ${ASSIMP_INCLUDE_DIRS}
    ${SPDLOG_INCLUDE_DIRS}
    ${FMT_INCLUDE_DIRS}
    ${NLOHMANN_JSON_INCLUDE_DIRS}
)

if(BUILD_STATIC)
    target_link_libraries(3dco+
        PRIVATE
        ${GLFW_STATIC_LIBRARIES}
        ${SDL3_STATIC_LIBRARIES}
        ${OPENGL_LIBRARIES}
        ${ASSIMP_STATIC_LIBRARIES}
        ${SPDLOG_STATIC_LIBRARIES}
        ${FMT_STATIC_LIBRARIES}
        ${THREAD_LIBRARY}
        # ZLIB is handled separately below
    )
else()
    target_link_libraries(3dco+
        PRIVATE
        ${GLFW_LIBRARIES}
        ${SDL3_LIBRARIES}
        ${OPENGL_LIBRARIES}
        ${ASSIMP_LIBRARIES}
        ${SPDLOG_LIBRARIES}
        ${FMT_LIBRARIES}
        ${THREAD_LIBRARY}
    )
endif()

# ====== macOS‑specific linkage ======
if(APPLE)
    target_link_options(3dco+ PRIVATE
        -L${CMAKE_PREFIX_PATH}/lib
    )

    target_link_libraries(3dco+ PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework ForceFeedback"
        "-framework CoreVideo"
        "-framework CoreMedia"
        "-framework GameController"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Carbon"
        "-framework CoreHaptics"
        "-framework Foundation"
        "-framework UniformTypeIdentifiers"
        "-lobjc"
        "-lresolv"
        # ZLIB is forced via force_load below
    )

    # Force static zlib: -Wl,-force_load forces the linker to include every
    # object from the specified static library, overriding any dynamic search.
    target_link_options(3dco+ PRIVATE -Wl,-force_load,${ZLIB_LIBRARY})
else()
    target_link_libraries(3dco+ PRIVATE
        ${OPENGL_LIBRARIES}
        ZLIB::ZLIB
    )
endif()

# ------------------------------------------------------------------
# macOS system tray icon (NSStatusItem via Cocoa - see tray_icon.cpp).
# tray_icon.cpp is a single file shared across all three platforms
# (differentiated internally via #if), so rather than maintaining a
# separate .mm file it's just compiled as Objective-C++ on this one
# platform. The Cocoa framework link above already covers the rest.
# ------------------------------------------------------------------
if(APPLE)
    set_source_files_properties(src/tray_icon.cpp PROPERTIES
        COMPILE_FLAGS "-x objective-c++"
    )
endif()

if(WIN32)
    # shell32: Shell_NotifyIconW (system tray icon, see tray_icon.cpp)
    target_link_libraries(3dco+ PRIVATE ws2_32 winmm user32 comctl32 shell32)
    
    if(NOT BUILD_STATIC)
        file(GLOB WINDOWS_REDIST "${CMAKE_SOURCE_DIR}/packaging/windows/redist/*.dll")
        foreach(dll ${WINDOWS_REDIST})
            add_custom_command(TARGET 3dco+ POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${dll}" "$<TARGET_FILE_DIR:3dco+>"
            )
        endforeach()
    endif()

    # Embed Windows icon resource
    target_sources(3dco+ PRIVATE packaging/windows/3dco+.rc)
endif()

# ------------------------------------------------------------------
# Linux system tray icon (StatusNotifierItem over D-Bus - see
# tray_icon.cpp). Optional: if dbus-1 isn't found, the build still
# succeeds and TrayIcon::isSupported() just returns false on this
# platform, matching how it already behaves on macOS today.
# ------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    pkg_check_modules(DBUS dbus-1)
    if(DBUS_FOUND)
        message(STATUS "Found dbus-1 - Linux system tray icon enabled")
        target_include_directories(3dco+ PRIVATE ${DBUS_INCLUDE_DIRS})
        target_link_libraries(3dco+ PRIVATE ${DBUS_LIBRARIES})
        target_compile_options(3dco+ PRIVATE ${DBUS_CFLAGS_OTHER})
        target_compile_definitions(3dco+ PRIVATE HAVE_DBUS=1)
    else()
        message(WARNING "dbus-1 not found - Linux system tray icon will be "
                        "unavailable (the app still builds and runs fine "
                        "without it).")
    endif()
endif()

# ------------------------------------------------------------------
# No need to copy gamecontrollerdb.txt at build time – it's embedded.
# ------------------------------------------------------------------

install(TARGETS 3dco+ RUNTIME DESTINATION bin)
# Install gamecontrollerdb.txt to share/3dco+/ for system-wide use (optional)
install(FILES ${DB_SOURCE} DESTINATION share/3dco+)
install(FILES assets/icon.png DESTINATION share/icons/hicolor/256x256/apps)
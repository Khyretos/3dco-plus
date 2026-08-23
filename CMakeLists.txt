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
# Collect all source files (including ImGui, miniz, glad, etc.)
# ------------------------------------------------------------------
file(GLOB_RECURSE SRC_FILES src/*.cpp src/*.c)
# Exclude backup folder if any
list(FILTER SRC_FILES EXCLUDE REGEX "src/imgui/backup/.*")

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

# ------------------------------------------------------------------
# Executable – uses the collected sources + generated headers
# ------------------------------------------------------------------
add_executable(3dco+ ${SRC_FILES} ${GEN_HEADER} ${DB_HEADER} ${ICON_HEADER})

# Include the source directory
target_include_directories(3dco+ PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
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
    ${CMAKE_SOURCE_DIR}/src/imgui
    ${CMAKE_SOURCE_DIR}/third_party/include
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

if(APPLE)
    target_link_options(3dco+ PRIVATE
        -L${CMAKE_PREFIX_PATH}/lib
    )

    target_link_libraries(3dco+ PRIVATE
        ${BASE_LIBS}
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
        "-lobjc"
        "-lresolv"
        ZLIB::ZLIB
    )
else()
    target_link_libraries(3dco+ PRIVATE
        ${BASE_LIBS}
        ${OPENGL_LIBRARIES}
        ZLIB::ZLIB
    )
endif()

if(WIN32)
    target_link_libraries(3dco+ PRIVATE ws2_32 winmm user32)
    
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
# No need to copy gamecontrollerdb.txt at build time – it's embedded.
# ------------------------------------------------------------------

install(TARGETS 3dco+ RUNTIME DESTINATION bin)
# Install gamecontrollerdb.txt to share/3dco+/ for system-wide use (optional)
install(FILES ${DB_SOURCE} DESTINATION share/3dco+)
install(FILES assets/icon.png DESTINATION share/icons/hicolor/256x256/apps)
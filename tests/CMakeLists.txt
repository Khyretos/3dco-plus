# Test harness for 3dco+. Included from the root CMakeLists.txt
# (BUILD_TESTS option, on by default).
#
# doctest.h is vendored at third_party/doctest/doctest.h (MIT
# licensed, pinned to v2.4.9 - see that file's own header comment).
# One header, no separate library to build or link.
#
# Deliberately its own small executable per logical group of tests
# rather than one giant test binary - keeps each test's dependency
# footprint honest and visible in this file, rather than one binary
# whose true dependency surface just keeps growing unnoticed.
#
# test_model_persistence only needs model_persistence.cpp, model.h,
# and nlohmann/json.hpp - NOT the rest of the app (no GLFW/SDL/
# assimp/OpenGL context needed). model.h itself pulls in glad/glad.h,
# glm/, and SDL3/SDL.h though (for GL type/constant stubs, matrix
# types, and Uint8 respectively) even though nothing here actually
# uses OpenGL, GLM math, or SDL at runtime - so those three still need
# to be findable, same as the main app target.
#
# test_combine_with_chain needs nothing but combine_with_chain.h
# itself (header-only, templated on the lookup callable) and the
# standard library - no StyleData, no disk I/O, no image decoding,
# despite testing the exact algorithm that isInputIgnored()
# (input_history_glyphs.cpp) now calls into for real.
#
# test_shortcut_logic needs shortcut_logic.cpp (getShortcutKind()'s
# real definition) plus SDL3 for SDL_Scancode - no GLFW, no ImGui, no
# GlobalKeyboard, despite testing the exact shortcutId -> key mapping
# isShortcutPhysicallyActive() (controller_window.cpp) now calls into
# for real via isShortcutPhysicallyActiveGeneric().
#
# Run after building: ctest --test-dir <build-dir> --output-on-failure
# or run a test binary directly for its own doctest output/options
# (--help lists them - -s for successes too, -tc=<name> to run one
# test case, etc).
#
# ---- Why src/ and third_party/doctest/ use -iquote, not -I ----
# target_include_directories() always emits a plain -I flag,
# regardless of whether the including file used "..." or <...>. -I
# paths are searched for BOTH quote and angle-bracket includes, and
# are searched before the system include path. This project has its
# own src/strings.h - and glibc's own <string.h> internally does
# #include <strings.h> (the POSIX header, plural) as part of its own
# chain. With src/ on a plain -I path, that angle-bracket lookup
# inside glibc's own header can resolve to this project's
# src/strings.h instead of the real /usr/include/strings.h, which
# cascades into "template with C linkage" errors throughout <string>/
# <tuple> (a C++ standard header's contents ending up parsed as if
# still inside strings.h's own extern "C" block, since that's what
# the wrong file lacks a matching guard against). -iquote searches
# its path ONLY for quote-style #include "..." - exactly what's
# needed to find "combine_with_chain.h" etc. by their relative
# quote-includes, without ever being consulted for glibc's own
# angle-bracket lookups.
#
# Passed as a single "-iquote<path>" token (no space), NOT the
# "-iquote" "<path>" two-token form. CMake's target_compile_options
# de-duplicates repeated known-prefix flags (like -iquote) across
# entries/calls, and with the two-token form this silently merges
# multiple -iquote invocations into one, dropping every "-iquote"
# after the first and leaving their paths bare on the command line
# (verified directly against the generated flags.make: two separate
# "-iquote" "<path>" pairs collapsed into a single
# "-iquote <path1> <path2>", which is not the same thing - only
# path1 is actually an -iquote argument, path2 is a stray token GCC
# ignores). The attached single-token form isn't subject to that
# merge and was confirmed to produce two independent, correct
# -iquote flags in the actual generated compile command.
#
# Applied to every target here, not just the one that happened to
# hit the underlying string.h/strings.h collision in practice - the
# other two only dodged it because their forced preinclude.h
# -include resolves <string> before anything reaches the vulnerable
# path, which is incidental protection, not a real fix.

add_executable(test_model_persistence
    test_model_persistence.cpp
    ${CMAKE_SOURCE_DIR}/src/model_persistence.cpp
)
target_include_directories(test_model_persistence PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/include # glad/glad.h, glm/ - see model_persistence's own note above
    ${SDL3_INCLUDE_DIRS}                    # SDL3/SDL.h - set by pkg_check_modules(SDL3 ...) in the root file
    ${NLOHMANN_JSON_INCLUDE_DIRS}           # nlohmann/json.hpp - set by pkg_check_modules(NLOHMANN_JSON ...) in the root file
)
# DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN: exactly one translation unit per
# test binary must define this before including doctest.h - it's what
# actually generates doctest's implementation (String, test
# registration, main() itself), not just declarations. Without it:
# "undefined reference to main" and to every doctest:: symbol at link
# time, which is exactly the failure this fixes.
target_compile_definitions(test_model_persistence PRIVATE DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
target_compile_options(test_model_persistence PRIVATE
    "-iquote${CMAKE_SOURCE_DIR}/src"
    "-iquote${CMAKE_SOURCE_DIR}/third_party/doctest"
    $<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_SOURCE_DIR}/src/preinclude.h>
)
target_compile_features(test_model_persistence PRIVATE cxx_std_17)

add_test(NAME model_persistence COMMAND test_model_persistence)

add_executable(test_combine_with_chain
    test_combine_with_chain.cpp
)
# See test_model_persistence's own note on this above - same fix,
# needed by every test binary that includes doctest.h.
target_compile_definitions(test_combine_with_chain PRIVATE DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
target_compile_options(test_combine_with_chain PRIVATE
    "-iquote${CMAKE_SOURCE_DIR}/src"
    "-iquote${CMAKE_SOURCE_DIR}/third_party/doctest"
)
target_compile_features(test_combine_with_chain PRIVATE cxx_std_17)

add_test(NAME combine_with_chain COMMAND test_combine_with_chain)

add_executable(test_shortcut_logic
    test_shortcut_logic.cpp
    ${CMAKE_SOURCE_DIR}/src/shortcut_logic.cpp
)
target_include_directories(test_shortcut_logic PRIVATE
    ${SDL3_INCLUDE_DIRS} # SDL3/SDL.h, for SDL_Scancode - header-only usage here, nothing actually calls into the SDL3 library at runtime, so no target_link_libraries needed
)
# See test_model_persistence's own note on this above - same fix,
# needed by every test binary that includes doctest.h.
target_compile_definitions(test_shortcut_logic PRIVATE DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
target_compile_options(test_shortcut_logic PRIVATE
    "-iquote${CMAKE_SOURCE_DIR}/src"
    "-iquote${CMAKE_SOURCE_DIR}/third_party/doctest"
    $<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_SOURCE_DIR}/src/preinclude.h>
)
target_compile_features(test_shortcut_logic PRIVATE cxx_std_17)

add_test(NAME shortcut_logic COMMAND test_shortcut_logic)

# test_version_utils needs nothing but version_utils.h itself
# (header-only, standard library only).
add_executable(test_version_utils
    test_version_utils.cpp
)
target_compile_definitions(test_version_utils PRIVATE DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
target_compile_options(test_version_utils PRIVATE
    "-iquote${CMAKE_SOURCE_DIR}/src"
    "-iquote${CMAKE_SOURCE_DIR}/third_party/doctest"
)
target_compile_features(test_version_utils PRIVATE cxx_std_17)

add_test(NAME version_utils COMMAND test_version_utils)

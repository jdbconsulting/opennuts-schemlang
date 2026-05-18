cmake_minimum_required(VERSION 3.24)
project(schemlang
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Schemlang syntax checker, LSP server and WASM library"
)

# ---------------------------------------------------------------------------
# C++ standard. The project targets C++26; in practice we only depend on
# C++20 features (string_view::starts_with, std::span-free, etc.), so on
# older toolchains we fall back gracefully. CMake will pick the highest
# standard the compiler advertises support for.
# ---------------------------------------------------------------------------
function(_schemlang_pick_cxx_std out_var)
    # Under emscripten the clang reports CXX26 support but CMake doesn't yet
    # know how to spell the right flag for it, so cap at 20 there.
    if(EMSCRIPTEN)
        set(${out_var} 20 PARENT_SCOPE)
        return()
    endif()
    set(_candidates 26 23 20)
    foreach(_n IN LISTS _candidates)
        if("cxx_std_${_n}" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
            set(${out_var} ${_n} PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} 20 PARENT_SCOPE)
endfunction()
_schemlang_pick_cxx_std(SCHEMLANG_CXX_STANDARD)
message(STATUS "Schemlang: building with C++${SCHEMLANG_CXX_STANDARD}")
set(CMAKE_CXX_STANDARD ${SCHEMLANG_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Standard install path conventions (bin/, share/doc/<project>/, etc.).
# Needed so ``cmake --install`` can stage a relocatable tree for the
# release tarballs produced by .github/workflows/release.yml.
include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Targets and options.
# ---------------------------------------------------------------------------
option(SCHEMLANG_BUILD_CLI  "Build the native CLI / LSP server"          ON)
option(SCHEMLANG_BUILD_WASM "Build the Embind WASM target (emscripten)"  OFF)

# Auto-enable the WASM target when configured under emscripten, and disable
# the native CLI in that case (emscripten produces .js + .wasm, not a
# normal native executable).
if(EMSCRIPTEN)
    set(SCHEMLANG_BUILD_WASM ON  CACHE BOOL "" FORCE)
    set(SCHEMLANG_BUILD_CLI  OFF CACHE BOOL "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies.
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    lexy
    GIT_REPOSITORY https://github.com/foonathan/lexy.git
    GIT_TAG        v2022.12.1
    GIT_SHALLOW    TRUE
)
set(LEXY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LEXY_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(LEXY_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(LEXY_BUILD_DOCS       OFF CACHE BOOL "" FORCE)
set(LEXY_BUILD_PACKAGE    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lexy)

# nlohmann/json -- used by the LSP server. Header-only.
if(SCHEMLANG_BUILD_CLI)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install    OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# ---------------------------------------------------------------------------
# Shared core sources (used by every target).
# ---------------------------------------------------------------------------
set(SCHEMLANG_CORE_SOURCES
    src/preprocessor.cpp
    src/lexer.cpp
    src/layout.cpp
    src/parser.cpp
    src/diagnostics.cpp
    src/driver.cpp
)

# ---------------------------------------------------------------------------
# Native CLI / LSP server.
# ---------------------------------------------------------------------------
if(SCHEMLANG_BUILD_CLI)
    add_executable(schemlang
        src/main.cpp
        src/lsp_server.cpp
        ${SCHEMLANG_CORE_SOURCES}
    )
    target_include_directories(schemlang PRIVATE src)
    target_link_libraries(schemlang PRIVATE foonathan::lexy nlohmann_json::nlohmann_json)
    target_compile_features(schemlang PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(schemlang PRIVATE /W4 /permissive-)
    else()
        target_compile_options(schemlang PRIVATE
            -Wall -Wextra -Wpedantic
            -Wno-missing-field-initializers
        )
    endif()

    # Install rules for the native CLI / LSP. ``cmake --install`` stages
    # the binary, the project README and the docs/ tree into the
    # configured prefix so release.yml can tar/zip a single self-
    # describing artifact per platform.
    install(TARGETS schemlang
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES README.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
    )
    install(DIRECTORY docs/
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
    )
endif()

# ---------------------------------------------------------------------------
# WASM target (emscripten cross-build).
# ---------------------------------------------------------------------------
if(SCHEMLANG_BUILD_WASM)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR
            "SCHEMLANG_BUILD_WASM=ON but the compiler is not emscripten. "
            "Re-configure with `emcmake cmake -S . -B build-wasm`.")
    endif()

    add_executable(schemlang_wasm
        src/wasm_entry.cpp
        ${SCHEMLANG_CORE_SOURCES}
    )
    target_include_directories(schemlang_wasm PRIVATE src)
    target_link_libraries(schemlang_wasm PRIVATE foonathan::lexy)
    target_compile_features(schemlang_wasm PRIVATE cxx_std_20)

    # The parser throws `ParseError` for recovery, so we need real C++
    # exceptions in the resulting wasm. Use native wasm exceptions on both
    # the compile and the link command.
    target_compile_options(schemlang_wasm PRIVATE -fwasm-exceptions)
    target_link_options(schemlang_wasm PRIVATE
        "-fwasm-exceptions"
        "--bind"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=createSchemlangModule"
        "-sENVIRONMENT=web,worker,node"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXPORT_ES6=0"
        "-sWASM=1"
        "-sNO_EXIT_RUNTIME=1"
    )

    # The default output name is schemlang_wasm.js with schemlang_wasm.wasm
    # alongside. Place them both under bin/ for symmetry with the CLI.
    set_target_properties(schemlang_wasm PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        SUFFIX ".js"
    )
endif()

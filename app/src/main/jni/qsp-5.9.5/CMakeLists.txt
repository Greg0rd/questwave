# Questwave native QSP adapter.
#
# The QSP 5.9.5 engine and Oniguruma are vendored under this directory at
# pinned commits (see README.md next to the sources), so the build does not
# touch the network and is reproducible.
#
# One shared library is produced: libndkqsp.so. It contains:
#   * the QSP 5.9.5 engine (MPL-2.0),
#   * the default (host-neutral) QSP binding,
#   * Oniguruma (BSD-2-Clause),
#   * androidqspwrapper.c -- the JNI adapter implementing the old
#     Questopia "NativeMethods" surface on top of the 5.9.5 API.

cmake_minimum_required(VERSION 3.22.1)

project(questwave_qsp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(GenerateExportHeader)

# ---------------------------------------------------------------------------
# Oniguruma (bundled, static)
# ---------------------------------------------------------------------------
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_POSIX_API OFF CACHE BOOL "" FORCE)
set(INSTALL_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)

# Same compile definition as upstream QSP's CMake build.
add_compile_definitions(USE_CRNL_AS_LINE_TERMINATOR=1)

add_subdirectory(oniguruma EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# QSP configuration header
# ---------------------------------------------------------------------------
set(qsp_VERSION "5.9.5")
set(QSP_GAMEMIN_VER "5.9.4")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/qsp_config.h.cmakein"
    "${CMAKE_CURRENT_BINARY_DIR}/qsp_config.h"
    @ONLY
)

# ---------------------------------------------------------------------------
# Engine + adapter
# ---------------------------------------------------------------------------
set(QSP_SOURCES
    qsp/actions.c
    qsp/callbacks.c
    qsp/codetools.c
    qsp/coding.c
    qsp/common.c
    qsp/errors.c
    qsp/game.c
    qsp/locations.c
    qsp/mathops.c
    qsp/memory.c
    qsp/memwatch.c
    qsp/menu.c
    qsp/objects.c
    qsp/playlist.c
    qsp/regexp.c
    qsp/statements.c
    qsp/text.c
    qsp/time.c
    qsp/towlower.c
    qsp/towupper.c
    qsp/tuples.c
    qsp/variables.c
    qsp/variant.c
    qsp/bindings/default/default_callbacks.c
    qsp/bindings/default/default_control.c
    androidqspwrapper.c
)

add_library(ndkqsp SHARED ${QSP_SOURCES})

set_target_properties(ndkqsp PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# QSP_CHAR is wchar_t. A 2-byte wchar_t makes QSP_CHAR identical to Java's
# jchar (UTF-16 code unit), so the adapter can copy strings without any
# transcoding.
target_compile_definitions(ndkqsp PRIVATE _UNICODE)

target_compile_options(ndkqsp PRIVATE
    -fshort-wchar
    -O3
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wno-char-subscripts
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-unused-function
    -Wno-parentheses
)

# NOTE: the qsp/ directory is *not* added as an include directory. It contains
# headers named like system headers (time.h), and -I would make <time.h>
# resolve to qsp/time.h, which in turn includes declarations.h -> recursion.
# The engine includes its own headers relative to the source files, and the
# wrapper uses the qsp/ prefix.
target_include_directories(ndkqsp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(ndkqsp PRIVATE onig log)

# 16 KB page size support (Android 15+). NDK r27 lld accepts the flag.
target_link_options(ndkqsp PRIVATE
    -Wl,--gc-sections
    -Wl,-z,max-page-size=16384
)

# qsp_export.h is included by qsp/bindings/qsp.h.
generate_export_header(ndkqsp
    BASE_NAME Qsp
    EXPORT_MACRO_NAME QSP_EXTERN
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/qsp_export.h"
)

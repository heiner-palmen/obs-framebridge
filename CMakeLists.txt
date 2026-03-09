cmake_minimum_required(VERSION 3.16)

# Read version from buildspec.json so there's a single source of truth
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" BUILDSPEC_JSON)
string(JSON PLUGIN_VERSION GET "${BUILDSPEC_JSON}" version)

project(obs-framebridge VERSION "${PLUGIN_VERSION}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Find OBS ─────────────────────────────────────────────────────────────────
# 1) OBS plugin-template / CI: OBS_DIR or CMAKE_PREFIX_PATH already set
# 2) System install: /usr/include/obs on Linux, ProgramFiles on Windows
# 3) Hint via -DLIBOBS_INCLUDE_DIR / -DLIBOBS_LIB

find_package(libobs QUIET)

if(NOT libobs_FOUND)
    # ------ manual fallback -----------------------------------------------
    if(NOT LIBOBS_INCLUDE_DIR)
        if(WIN32)
            set(_obs_default_inc "$ENV{ProgramFiles}/obs-studio/include")
        else()
            set(_obs_default_inc "/usr/include/obs")
        endif()
        find_path(LIBOBS_INCLUDE_DIR obs-module.h
            PATHS "${_obs_default_inc}" "/usr/local/include/obs"
            DOC "Path to OBS Studio include directory")
    endif()

    if(NOT LIBOBS_LIB)
        if(WIN32)
            find_library(LIBOBS_LIB obs
                PATHS "$ENV{ProgramFiles}/obs-studio/bin/64bit"
                DOC "Path to obs.lib/obs.dll import library")
        else()
            find_library(LIBOBS_LIB obs
                PATHS "/usr/lib" "/usr/local/lib"
                DOC "Path to libobs.so")
        endif()
    endif()

    # ── Windows auto-bootstrap ────────────────────────────────────────────
    # The standard OBS Windows installer ships no headers or import libs.
    # We auto-fix this by:
    #   1. Downloading the OBS source tarball from GitHub for headers.
    #   2. Generating obs.lib from the installed obs.dll via MSVC dumpbin+lib.
    if(WIN32 AND (NOT LIBOBS_INCLUDE_DIR OR NOT LIBOBS_LIB))
        # Read OBS version from buildspec.json (already read above)
        string(JSON _obs_ver GET "${BUILDSPEC_JSON}" dependencies obs-studio version)
        message(STATUS "Auto-bootstrapping OBS dev files for version ${_obs_ver}...")

        # ── 1. Headers: download + extract the official GitHub tarball ────
        set(_obs_archive  "${CMAKE_CURRENT_BINARY_DIR}/_obs_src.tar.gz")
        set(_obs_src_root "${CMAKE_CURRENT_BINARY_DIR}/_obs_src")
        set(_obs_inc_dir  "${_obs_src_root}/obs-studio-${_obs_ver}/libobs")

        # OBS ≤30: UI/obs-frontend-api/   OBS ≥31: frontend/api/
        set(_obs_fe_inc_dir_old "${_obs_src_root}/obs-studio-${_obs_ver}/UI/obs-frontend-api")
        set(_obs_fe_inc_dir_new "${_obs_src_root}/obs-studio-${_obs_ver}/frontend/api")

        if(NOT EXISTS "${_obs_inc_dir}/obs-module.h")
            message(STATUS "Downloading OBS ${_obs_ver} source tarball (headers only needed)...")
            file(DOWNLOAD
                "https://github.com/obsproject/obs-studio/archive/refs/tags/${_obs_ver}.tar.gz"
                "${_obs_archive}"
                SHOW_PROGRESS
                STATUS _dl_status
                TLS_VERIFY ON
            )
            list(GET _dl_status 0 _dl_code)
            if(NOT _dl_code EQUAL 0)
                list(GET _dl_status 1 _dl_msg)
                message(FATAL_ERROR "Failed to download OBS tarball: ${_dl_msg}\n"
                    "Set -DLIBOBS_INCLUDE_DIR=<path> and -DLIBOBS_LIB=<path> manually.")
            endif()

            # Probe archive to detect frontend-api path (changed in OBS 31)
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E tar tf "${_obs_archive}"
                OUTPUT_VARIABLE _tar_list
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _tar_rc
            )
            if(_tar_list MATCHES "obs-studio-[^/]+/frontend/api/")
                set(_fe_pattern "obs-studio-${_obs_ver}/frontend/api/*")
            else()
                set(_fe_pattern "obs-studio-${_obs_ver}/UI/obs-frontend-api/*")
            endif()

            message(STATUS "Extracting OBS headers from tarball (frontend-api: ${_fe_pattern})...")
            file(ARCHIVE_EXTRACT
                INPUT       "${_obs_archive}"
                DESTINATION "${_obs_src_root}"
                PATTERNS    "obs-studio-${_obs_ver}/libobs/*"
                             "${_fe_pattern}"
            )
        endif()

        # Resolve frontend-api include dir (whichever path was extracted)
        if(EXISTS "${_obs_fe_inc_dir_new}/obs-frontend-api.h")
            set(_obs_fe_inc_dir "${_obs_fe_inc_dir_new}")
        elseif(EXISTS "${_obs_fe_inc_dir_old}/obs-frontend-api.h")
            set(_obs_fe_inc_dir "${_obs_fe_inc_dir_old}")
        else()
            # Frontend-api not yet extracted from cached archive — extract it
            if(EXISTS "${_obs_archive}")
                execute_process(
                    COMMAND "${CMAKE_COMMAND}" -E tar tf "${_obs_archive}"
                    OUTPUT_VARIABLE _tar_list
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                )
                if(_tar_list MATCHES "obs-studio-[^/]+/frontend/api/")
                    set(_fe_pattern "obs-studio-${_obs_ver}/frontend/api/*")
                    set(_obs_fe_inc_dir "${_obs_fe_inc_dir_new}")
                else()
                    set(_fe_pattern "obs-studio-${_obs_ver}/UI/obs-frontend-api/*")
                    set(_obs_fe_inc_dir "${_obs_fe_inc_dir_old}")
                endif()
                message(STATUS "Extracting missing obs-frontend-api headers...")
                file(ARCHIVE_EXTRACT
                    INPUT       "${_obs_archive}"
                    DESTINATION "${_obs_src_root}"
                    PATTERNS    "${_fe_pattern}"
                )
            endif()
        endif()

        if(EXISTS "${_obs_inc_dir}/obs-module.h")
            set(LIBOBS_INCLUDE_DIR "${_obs_inc_dir}" CACHE PATH
                "Path to OBS Studio include directory (auto-downloaded)" FORCE)
            message(STATUS "OBS headers: ${LIBOBS_INCLUDE_DIR}")
        else()
            message(FATAL_ERROR
                "Extraction succeeded but obs-module.h not found in ${_obs_inc_dir}.\n"
                "Set -DLIBOBS_INCLUDE_DIR=<path> manually.")
        endif()

        # Generate obsconfig.h if not present (it's normally produced by the
        # OBS CMake build; for a plugin-only build a minimal stub suffices)
        if(NOT EXISTS "${_obs_inc_dir}/obsconfig.h")
            file(WRITE "${_obs_inc_dir}/obsconfig.h"
                "#pragma once\n"
                "/* Minimal stub generated by obs-framebridge CMakeLists.txt */\n"
                "/* OBS_DATA_PATH / OBS_PLUGIN_PATH not needed for plugin builds */\n"
                "#define OBS_RELEASE_CANDIDATE 0\n"
                "#define OBS_BETA 0\n"
            )
            message(STATUS "Generated stub obsconfig.h in ${_obs_inc_dir}")
        endif()

        # ── 2. Import libs: generate from installed DLLs ────────────────
        # Helper macro: generate <stem>.lib from <obs_bin_dir>/<stem>.dll
        macro(_obs_gen_implib stem out_var)
            set(_dll "$ENV{ProgramFiles}/obs-studio/bin/64bit/${stem}.dll")
            if(NOT EXISTS "${_dll}")
                message(FATAL_ERROR "${stem}.dll not found at ${_dll}.")
            endif()
            if(NOT DEFINED DUMPBIN_EXE OR NOT DEFINED LIB_EXE)
                cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH _msvc_bin)
                find_program(DUMPBIN_EXE NAMES dumpbin HINTS "${_msvc_bin}" NO_DEFAULT_PATH)
                find_program(LIB_EXE     NAMES lib     HINTS "${_msvc_bin}" NO_DEFAULT_PATH)
                if(NOT DUMPBIN_EXE OR NOT LIB_EXE)
                    message(FATAL_ERROR
                        "Could not find dumpbin/lib in ${_msvc_bin}.\n"
                        "Set -DLIBOBS_LIB=<path> to an existing obs.lib.")
                endif()
            endif()
            set(_def "${CMAKE_CURRENT_BINARY_DIR}/${stem}.def")
            set(_lib "${CMAKE_CURRENT_BINARY_DIR}/${stem}.lib")
            message(STATUS "Generating ${stem}.lib from ${_dll}...")
            execute_process(
                COMMAND "${DUMPBIN_EXE}" /EXPORTS "${_dll}"
                OUTPUT_VARIABLE _dump
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _rc)
            if(NOT _rc EQUAL 0)
                message(FATAL_ERROR "dumpbin failed for ${stem} (exit ${_rc}).")
            endif()
            string(REGEX MATCHALL
                "[ \t]+[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([A-Za-z_?@$][A-Za-z0-9_?@$]*)"
                _matches "${_dump}")
            set(_def_txt "LIBRARY ${stem}\r\nEXPORTS\r\n")
            foreach(_m IN LISTS _matches)
                string(REGEX REPLACE ".*[ \t]([A-Za-z_?@$][A-Za-z0-9_?@$]*)$" "\\1" _s "${_m}")
                string(APPEND _def_txt "    ${_s}\r\n")
            endforeach()
            file(WRITE "${_def}" "${_def_txt}")
            execute_process(
                COMMAND "${LIB_EXE}" /MACHINE:X64 "/DEF:${_def}" "/OUT:${_lib}"
                ERROR_VARIABLE _lib_err
                RESULT_VARIABLE _lib_rc)
            if(NOT _lib_rc EQUAL 0 OR NOT EXISTS "${_lib}")
                message(FATAL_ERROR "lib.exe failed for ${stem}: ${_lib_err}")
            endif()
            set(${out_var} "${_lib}")
            message(STATUS "  -> ${_lib}")
        endmacro()

        if(NOT LIBOBS_LIB)
            _obs_gen_implib(obs LIBOBS_LIB)
            set(LIBOBS_LIB "${LIBOBS_LIB}" CACHE FILEPATH
                "Path to obs.lib (generated from obs.dll)" FORCE)
        endif()

        if(NOT LIBOBS_FRONTEND_LIB)
            _obs_gen_implib(obs-frontend-api LIBOBS_FRONTEND_LIB)
            set(LIBOBS_FRONTEND_LIB "${LIBOBS_FRONTEND_LIB}" CACHE FILEPATH
                "Path to obs-frontend-api.lib (generated from dll)" FORCE)
        endif()

        # Frontend-api include dir
        set(LIBOBS_FRONTEND_INCLUDE_DIR "${_obs_fe_inc_dir}" CACHE PATH
            "Path to obs-frontend-api include directory" FORCE)
    endif()
    # ── end Windows auto-bootstrap ────────────────────────────────────────

    if(NOT LIBOBS_INCLUDE_DIR OR NOT LIBOBS_LIB)
        message(FATAL_ERROR
            "Could not find OBS Studio. Set -DLIBOBS_INCLUDE_DIR=... and "
            "-DLIBOBS_LIB=... or install OBS Studio to the default path.")
    endif()

    add_library(OBS::libobs UNKNOWN IMPORTED)
    set_target_properties(OBS::libobs PROPERTIES
        IMPORTED_LOCATION "${LIBOBS_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_INCLUDE_DIR}")

    if(LIBOBS_FRONTEND_LIB)
        add_library(OBS::frontend-api UNKNOWN IMPORTED)
        set_target_properties(OBS::frontend-api PROPERTIES
            IMPORTED_LOCATION "${LIBOBS_FRONTEND_LIB}")
        if(LIBOBS_FRONTEND_INCLUDE_DIR)
            set_target_properties(OBS::frontend-api PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_FRONTEND_INCLUDE_DIR}")
        endif()
    endif()
endif()

# ─── Plugin target ────────────────────────────────────────────────────────────
add_library(obs-framebridge MODULE
    src/plugin-main.cpp
    src/frame-capture.cpp
    src/reference-image.cpp
    src/lua-api.cpp
    src/screenshot.cpp
)

# stb_image is a header-only C library bundled under third_party/stb/
target_include_directories(obs-framebridge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

target_link_libraries(obs-framebridge PRIVATE OBS::libobs
    $<TARGET_NAME_IF_EXISTS:OBS::frontend-api>)

target_compile_options(obs-framebridge PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W3>
)

# Remove the default "lib" prefix on Linux
set_target_properties(obs-framebridge PROPERTIES PREFIX "")

# ─── Install ──────────────────────────────────────────────────────────────────
# Detect the correct OBS plugin directory layout:
#
#   System package (Ubuntu/Debian apt):
#     binary  → /usr/lib/x86_64-linux-gnu/obs-plugins/
#     data    → /usr/share/obs/obs-plugins/<plugin>/
#
#   Windows (OBS 31+ / 32+):
#     OBSBasic::AddExtraModulePaths() uses GetProgramDataPath (CSIDL_COMMON_APPDATA):
#       binary  → %ProgramData%/obs-studio/plugins/<module>/bin/64bit/
#       data    → %ProgramData%/obs-studio/plugins/<module>/data/
#     NOTE: %APPDATA%/obs-studio/obs-plugins/ is NOT scanned by OBS 31+.
#
#   Linux user / portable:
#               ~/.config/obs-studio/obs-plugins/64bit/    (Linux user)
#     data    → corresponding data/obs-plugins dir
#
# Override by passing -DOBS_PLUGIN_DESTINATION=... / -DOBS_DATA_DESTINATION=...

if(WIN32)
    set(_default_plugin_dir "$ENV{ProgramData}/obs-studio/plugins/obs-framebridge/bin/64bit")
    set(_default_data_dir   "$ENV{ProgramData}/obs-studio/plugins/obs-framebridge/data")
elseif(EXISTS "/usr/lib/x86_64-linux-gnu/obs-plugins")
    # Debian/Ubuntu system package layout (autodetected)
    set(_default_plugin_dir "/usr/lib/x86_64-linux-gnu/obs-plugins")
    set(_default_data_dir   "/usr/share/obs/obs-plugins/obs-framebridge")
elseif(EXISTS "/usr/lib/obs-plugins")
    # Some distros use this path
    set(_default_plugin_dir "/usr/lib/obs-plugins")
    set(_default_data_dir   "/usr/share/obs/obs-plugins/obs-framebridge")
else()
    # Fallback: user-local install
    set(_default_plugin_dir "$ENV{HOME}/.config/obs-studio/obs-plugins/64bit")
    set(_default_data_dir   "$ENV{HOME}/.config/obs-studio/data/obs-plugins/obs-framebridge")
endif()

set(OBS_PLUGIN_DESTINATION "${_default_plugin_dir}"
    CACHE PATH "Directory where obs-framebridge.so/.dll is installed")
set(OBS_DATA_DESTINATION "${_default_data_dir}"
    CACHE PATH "Directory where obs-framebridge data/locale files are installed")

message(STATUS "obs-framebridge install paths:")
message(STATUS "  plugin binary : ${OBS_PLUGIN_DESTINATION}")
message(STATUS "  plugin data   : ${OBS_DATA_DESTINATION}")

install(TARGETS obs-framebridge
    LIBRARY  DESTINATION "${OBS_PLUGIN_DESTINATION}"
    RUNTIME  DESTINATION "${OBS_PLUGIN_DESTINATION}"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/"
    DESTINATION "${OBS_DATA_DESTINATION}"
)

# ─── Helper target: install directly ──────────────────────────────────────────
add_custom_target(install-plugin
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
    COMMENT "Installing obs-framebridge to OBS plugin directory"
    VERBATIM
)

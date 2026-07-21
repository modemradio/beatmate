cmake_minimum_required(VERSION 3.22)
project(BeatMateV11
    VERSION 12.0.24
    DESCRIPTION "BeatMate V12 - Professional DJ Application"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Epoch de build (UTC) pour la detection de mise a jour "par date du fichier".
string(TIMESTAMP BM_BUILD_EPOCH "%s" UTC)

# Crash-diag: generate PDB symbols for Release builds so minidumps from
# production can be decoded post-mortem without rebuilding in Debug.
if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options($<$<CONFIG:Release>:/DEBUG:FULL>)
    add_link_options($<$<CONFIG:Release>:/OPT:REF>)
    add_link_options($<$<CONFIG:Release>:/OPT:ICF>)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Build options
option(BEATMATE_USE_ONNX_GPU "Use the GPU build of ONNX Runtime (CUDA, ~400MB) instead of CPU-only (~10MB)" OFF)
# [COPIE MAC] PerfDJ désactivé : module Windows externe non copié dans cette version Mac.
option(BEATMATE_ENABLE_PERFDJ "Link the external BeatMateProPerfDJ module (requires its JUCE 8 port)" OFF)

# Include cmake modules
include(cmake/CompilerFlags.cmake)
include(cmake/Dependencies.cmake)

# ============================================================
# BeatMateCore - Static library (audio engine, DSP, analysis)
# ============================================================
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    src/core/*.cpp
    src/core/*.h
)
add_library(BeatMateCore STATIC ${CORE_SOURCES})
target_include_directories(BeatMateCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(BeatMateCore PUBLIC
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    SoundTouch::SoundTouch
    kissfft::kissfft
    TagLib::TagLib
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    sqlite3
    onnxruntime
)
if(TARGET signalsmith-stretch)
    target_link_libraries(BeatMateCore PUBLIC signalsmith-stretch)
endif()
target_compile_definitions(BeatMateCore PUBLIC
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
)

# ============================================================
# BeatMateServices - Static library (business logic)
# ============================================================
file(GLOB_RECURSE SERVICES_SOURCES CONFIGURE_DEPENDS
    src/services/*.cpp
    src/services/*.h
    src/models/*.h
)
add_library(BeatMateServices STATIC ${SERVICES_SOURCES})
target_include_directories(BeatMateServices PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(BeatMateServices PUBLIC
    BeatMateCore
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_cryptography
    juce::juce_osc
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    TagLib::TagLib
)
if(WIN32)
    target_link_libraries(BeatMateServices PUBLIC ws2_32)
endif()
target_compile_definitions(BeatMateServices PUBLIC BEATMATE_BUILD_EPOCH=${BM_BUILD_EPOCH})

# ============================================================
# BeatMateLicense - Shared license stack (SonicDeck, Jingle, PerfDJ, Live)
#
# Compiles the minimal activation/HWID/encryption stack + the reusable JUCE
# license dialog so EVERY app in the suite gates on the SAME shared license
# store (%APPDATA%/BeatMate/license.key + trial.dat). V11 keeps its own copy
# through BeatMateServices; no executable links both, so there is no ODR clash.
# ============================================================
set(BEATMATE_WP_API_KEY_VALUE "" CACHE STRING
    "Production WordPress API key baked into the suite (optional; wp.json overrides at runtime)")

add_library(BeatMateLicense STATIC
    src/services/security/HardwareId.cpp
    src/services/security/EncryptionService.cpp
    src/services/security/LicenseValidator.cpp
    src/services/security/LicenseService.cpp
    src/services/network/HttpClient.cpp
    src/services/wordpress/WordPressLicenseClient.cpp
    src/services/wordpress/LicenseHeartbeatService.cpp
    src/shared/license/SharedLicense.cpp
    src/shared/license/LicenseDialogShared.cpp
)
target_include_directories(BeatMateLicense PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(BeatMateLicense PUBLIC
    juce::juce_core
    juce::juce_events
    juce::juce_cryptography
    juce::juce_gui_basics
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
target_compile_definitions(BeatMateLicense PRIVATE BEATMATE_SUITE_VERSION="${PROJECT_VERSION}")
if(BEATMATE_WP_API_KEY_VALUE)
    target_compile_definitions(BeatMateLicense PUBLIC BEATMATE_WP_API_KEY="${BEATMATE_WP_API_KEY_VALUE}")
endif()
if(WIN32)
    target_link_libraries(BeatMateLicense PUBLIC ws2_32 wbemuuid crypt32)
endif()

# ============================================================
# BeatMateBranding - Embedded logo binary data
# ============================================================
juce_add_binary_data(BeatMateBranding
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/beatmate_logo_full.png"
)

# ============================================================
# BeatMateFonts - Embedded UI fonts (Inter + JetBrains Mono, OFL)
# ============================================================
juce_add_binary_data(BeatMateFonts
    HEADER_NAME BeatMateFontsData.h
    NAMESPACE BeatMateFontsData
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/InterRegular.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/InterMedium.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/InterSemiBold.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/InterBold.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/JetBrainsMonoRegular.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/JetBrainsMonoMedium.ttf"
)

# ============================================================
# BeatMateUI - Static library (JUCE UI)
# ============================================================
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS
    src/ui/*.cpp
    src/ui/*.h
)
add_library(BeatMateUI STATIC ${UI_SOURCES})
target_include_directories(BeatMateUI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(BeatMateUI PUBLIC
    BeatMateServices
    BeatMateBranding
    BeatMateFonts
    $<$<TARGET_EXISTS:BeatMatePerfDJ>:BeatMatePerfDJ>
    juce::juce_core
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_graphics
    juce::juce_opengl
    juce::juce_audio_utils
)

# ============================================================
# BeatMatePerfDJ - External PerfDJ Module
# ============================================================
set(PERFDJ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../BeatMateProPerfDJ")
if(BEATMATE_ENABLE_PERFDJ AND EXISTS "${PERFDJ_DIR}/CMakeLists_lib.cmake")
    include("${PERFDJ_DIR}/CMakeLists_lib.cmake")
    message(STATUS "PerfDJ module: ENABLED")
else()
    message(STATUS "PerfDJ module: DISABLED (set -DBEATMATE_ENABLE_PERFDJ=ON after porting it to JUCE 8)")
endif()

if(WIN32)
    file(GLOB BM_WEBVIEW2_HEADERS
        "$ENV{USERPROFILE}/.nuget/packages/microsoft.web.webview2/*/build/native/include/WebView2.h"
        "$ENV{USERPROFILE}/AppData/Local/PackageManagement/NuGet/Packages/*Microsoft.Web.WebView2*/build/native/include/WebView2.h")
    if(BM_WEBVIEW2_HEADERS)
        list(SORT BM_WEBVIEW2_HEADERS COMPARE NATURAL)
        list(GET BM_WEBVIEW2_HEADERS -1 BM_WEBVIEW2_HEADER)
        get_filename_component(BM_WEBVIEW2_INCLUDE "${BM_WEBVIEW2_HEADER}" DIRECTORY)
        get_filename_component(BM_WEBVIEW2_NATIVE "${BM_WEBVIEW2_INCLUDE}" DIRECTORY)
        set(BM_WEBVIEW2_LIB "${BM_WEBVIEW2_NATIVE}/x64/WebView2LoaderStatic.lib")
        if(EXISTS "${BM_WEBVIEW2_LIB}")
            foreach(bm_wv2_target IN ITEMS BeatMateUI BeatMatePerfDJ)
                if(TARGET ${bm_wv2_target})
                    target_compile_definitions(${bm_wv2_target} PRIVATE
                        JUCE_USE_WIN_WEBVIEW2=1
                        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
                    target_include_directories(${bm_wv2_target} PRIVATE "${BM_WEBVIEW2_INCLUDE}")
                endif()
            endforeach()
            target_link_libraries(BeatMateUI PUBLIC "${BM_WEBVIEW2_LIB}")
            message(STATUS "WebView2 SDK: ${BM_WEBVIEW2_NATIVE}")
        else()
            message(STATUS "WebView2 SDK: loader lib missing, embed YouTube disabled (browser fallback)")
        endif()
    else()
        message(STATUS "WebView2 SDK: not found, embed YouTube disabled (browser fallback)")
    endif()
endif()

# ============================================================
# BeatMateV11 - Main JUCE Application
# ============================================================
juce_add_gui_app(BeatMateV11
    PRODUCT_NAME "BeatMate V11"
    COMPANY_NAME "BeatMate"
    BUNDLE_ID "app.beatmate.v11"
    VERSION "${PROJECT_VERSION}"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/beatmate_logo.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/beatmate_logo.png"
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
)

if(APPLE)
    set_target_properties(BeatMateV11 PROPERTIES
        BUILD_RPATH "@executable_path"
        INSTALL_RPATH "@executable_path")
endif()

target_sources(BeatMateV11 PRIVATE
    src/main.cpp
    src/app/Application.cpp
    src/app/Application.h
    src/app/ServiceLocator.cpp
    src/app/ServiceLocator.h
    src/app/Config.cpp
    src/app/Config.h
    src/app/NativeSplash.cpp
    src/app/NativeSplash.h
)

target_include_directories(BeatMateV11 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(BeatMateV11 PRIVATE
    BeatMateUI
    BeatMateServices
    BeatMateCore
    $<$<TARGET_EXISTS:BeatMatePerfDJ>:BeatMatePerfDJ>
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(BeatMateV11 PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=1
)

# ------------------------------------------------------------------------------
# /FORCE:MULTIPLE — TECHNICAL DEBT (do not leave forever)
# ------------------------------------------------------------------------------
# The external BeatMatePerfDJ module historically reused several class names
# already defined in src/core and src/services (e.g. BeatGridGenerator, AudioPlayer
# variants). When both are statically linked into BeatMateV11.exe, the MSVC linker
# emits LNK2005 (symbol already defined) ODR violations.
#
# /FORCE:MULTIPLE silences these errors by letting the linker pick the first
# definition it sees. This is a SYMPTOM, not a cure: the program ends up with
# whichever vtable came first, which can cause subtle runtime divergence.
#
# TODO(perfdj): namespace or rename the duplicated PerfDJ classes (prefix them
# with `perfdj::` or `PerfDJ_` ) so each translation unit resolves to a single
# definition, then remove this flag. Tracked as technical-debt item.
# ------------------------------------------------------------------------------
if(TARGET BeatMatePerfDJ AND MSVC)
    target_link_options(BeatMateV11 PRIVATE /FORCE:MULTIPLE)
endif()

# Copy resources to build output
add_custom_command(TARGET BeatMateV11 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/resources $<TARGET_FILE_DIR:BeatMateV11>/resources
)

# ============================================================
# External tool bundling (LAME + FFmpeg)
#
# LAME is required for MP3 export; FFmpeg for MP4 video export. Both are
# LGPL and not embedded into BeatMate sources. Instead, we auto-fetch them
# at configure-time via scripts/fetch_external_tools.ps1 (Windows only).
#
# The fetched binaries go into tools/external/ (gitignored) and are then
# copied next to BeatMate V11.exe at build time so the app finds them.
# ============================================================
set(BM_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/external")

if(WIN32 AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(NOT EXISTS "${BM_TOOLS_DIR}/lame.exe" OR NOT EXISTS "${BM_TOOLS_DIR}/ffmpeg.exe")
        message(STATUS "Fetching external tools (LAME + FFmpeg)...")
        execute_process(
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch_external_tools.ps1"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE BM_FETCH_RC
            OUTPUT_VARIABLE BM_FETCH_OUT
            ERROR_VARIABLE  BM_FETCH_ERR
        )
        if(NOT BM_FETCH_RC EQUAL 0)
            message(WARNING "fetch_external_tools.ps1 failed (rc=${BM_FETCH_RC}). "
                            "MP3 and MP4 export will be disabled at runtime. "
                            "See docs/INSTALL_EXTERNAL_TOOLS.md for manual install.")
        endif()
    endif()
endif()

# Copy the fetched binaries next to BeatMate V11.exe after each build so the
# app finds them via its runtime lookup (exe dir -> tools/ -> PATH).
foreach(tool IN ITEMS lame.exe ffmpeg.exe)
    if(EXISTS "${BM_TOOLS_DIR}/${tool}")
        add_custom_command(TARGET BeatMateV11 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BM_TOOLS_DIR}/${tool}"
            "$<TARGET_FILE_DIR:BeatMateV11>/${tool}"
            COMMENT "Bundling ${tool} next to BeatMate V11.exe")
    endif()
endforeach()

# Bundle the SOTA stem-separation engine (stemsep.exe + models + _internal)
# next to BeatMate V11.exe so the Ultra (MDX-GPU) path finds it via its runtime
# lookup (exe dir -> tools/external/stemsep/). The folder is large (~2 GB with
# models) and gitignored, so it is only copied when present in the source tree.
if(EXISTS "${BM_TOOLS_DIR}/stemsep/stemsep.exe")
    add_custom_command(TARGET BeatMateV11 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${BM_TOOLS_DIR}/stemsep"
        "$<TARGET_FILE_DIR:BeatMateV11>/tools/external/stemsep"
        COMMENT "Bundling stemsep engine next to BeatMate V11.exe")
endif()

# Copy ONNX Runtime DLLs next to BeatMate V11.exe. The copy in Dependencies.cmake
# was guarded by if(TARGET BeatMateV11) but ran before the target was created, so
# it never registered and the app failed to start (missing onnxruntime.dll). Do it
# here where the target exists.
if(DEFINED onnxruntime_SOURCE_DIR)
    file(GLOB BM_ONNX_DLLS "${onnxruntime_SOURCE_DIR}/lib/*.dll")
    foreach(DLL ${BM_ONNX_DLLS})
        add_custom_command(TARGET BeatMateV11 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DLL}" "$<TARGET_FILE_DIR:BeatMateV11>"
            COMMENT "Bundling ONNX Runtime DLL next to BeatMate V11.exe")
    endforeach()
endif()

# CLAP audio-embedding models (CC0 music_audioset = similarite ; larger_clap_music
# Apache-2.0 = tags genre/mood). Copied next to the exe so ClapEncoder finds them;
# suggestions degrade gracefully to metadata-only scoring when absent.
set(BM_CLAP_MODEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/models/clap")
if(EXISTS "${BM_CLAP_MODEL_DIR}/audio_model.onnx")
    add_custom_command(TARGET BeatMateV11 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:BeatMateV11>/resources/models/clap"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BM_CLAP_MODEL_DIR}/audio_model.onnx"
            "${BM_CLAP_MODEL_DIR}/mel_slaney.npy"
            "$<TARGET_FILE_DIR:BeatMateV11>/resources/models/clap"
        COMMENT "Bundling CLAP similarity model next to BeatMate V11.exe")
endif()
set(BM_CLAP_TAGS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/models/clap_tags")
if(EXISTS "${BM_CLAP_TAGS_DIR}/audio_model.onnx")
    add_custom_command(TARGET BeatMateV11 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:BeatMateV11>/resources/models/clap_tags"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BM_CLAP_TAGS_DIR}/audio_model.onnx"
            "${BM_CLAP_TAGS_DIR}/mel_slaney.npy"
            "${BM_CLAP_TAGS_DIR}/genre_labels.bin"
            "${BM_CLAP_TAGS_DIR}/mood_labels.bin"
            "$<TARGET_FILE_DIR:BeatMateV11>/resources/models/clap_tags"
        COMMENT "Bundling CLAP tagging model next to BeatMate V11.exe")
endif()

juce_add_console_app(BeatMateClapProbe PRODUCT_NAME "BeatMateClapProbe")
target_sources(BeatMateClapProbe PRIVATE tools/ClapProbe.cpp)
target_include_directories(BeatMateClapProbe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(BeatMateClapProbe PRIVATE
    BeatMateServices
    juce::juce_recommended_config_flags
)
target_compile_definitions(BeatMateClapProbe PRIVATE NOMINMAX _USE_MATH_DEFINES)
if(DEFINED onnxruntime_SOURCE_DIR)
    foreach(DLL ${BM_ONNX_DLLS})
        add_custom_command(TARGET BeatMateClapProbe POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DLL}" "$<TARGET_FILE_DIR:BeatMateClapProbe>"
            COMMENT "Bundling ONNX Runtime DLL next to BeatMateClapProbe.exe")
    endforeach()
endif()

# ============================================================
# Tests
# ============================================================
# OFF by default to avoid pulling Catch2 + failing on `include(Catch)` when the
# generator/scope hasn't propagated CMAKE_MODULE_PATH yet. CI and devs pass
# -DBUILD_TESTS=ON explicitly. tests/CMakeLists.txt also re-fetches Catch2 as
# a defensive fallback.
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================
# Install / Package
# ============================================================
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    install(TARGETS BeatMateV11
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        BUNDLE DESTINATION .
    )
    install(DIRECTORY resources/ DESTINATION share/beatmate/resources)

    foreach(tool IN ITEMS lame.exe ffmpeg.exe)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/external/${tool}")
            install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/tools/external/${tool}"
                    DESTINATION bin)
        endif()
    endforeach()

    add_executable(LicenseManager
        tools/LicenseManager.cpp
    )
    target_link_libraries(LicenseManager PRIVATE sqlite3)
    target_compile_definitions(LicenseManager PRIVATE NOMINMAX _USE_MATH_DEFINES)
    if(MSVC)
        target_compile_options(LicenseManager PRIVATE /W3)
    endif()
    install(TARGETS LicenseManager RUNTIME DESTINATION bin)

    if(WIN32)
        include(cmake/Packaging.cmake)
    endif()
endif()

# Dependencies.cmake - All external dependencies via FetchContent
# JUCE-based build - no Qt6 required

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# ============================================================
# JUCE - Audio application framework (replaces Qt6 + PortAudio + libremidi)
# ============================================================
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ============================================================
# SoundTouch - Time-stretch and pitch-shift
# ============================================================
FetchContent_Declare(soundtouch
    GIT_REPOSITORY https://codeberg.org/soundtouch/soundtouch.git
    GIT_TAG        2.3.3
)
set(SOUNDTOUCH_DLL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(soundtouch)
if(NOT TARGET SoundTouch::SoundTouch)
    add_library(SoundTouch::SoundTouch ALIAS SoundTouch)
endif()

# ============================================================
# KissFFT - Simple real-time FFT (use JUCE DSP FFT as fallback)
# ============================================================
FetchContent_Declare(kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG        131.1.0
    GIT_SHALLOW    TRUE
)
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(KISSFFT_STATIC ON CACHE BOOL "" FORCE)
set(KISSFFT_TEST OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS OFF CACHE BOOL "" FORCE)
set(KISSFFT_DATATYPE "float" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(kissfft)
if(NOT TARGET kissfft::kissfft)
    if(TARGET kissfft)
        add_library(kissfft::kissfft ALIAS kissfft)
    else()
        # Fallback: create empty interface, use JUCE FFT instead
        message(STATUS "KissFFT not available - using JUCE DSP FFT")
        add_library(kissfft::kissfft INTERFACE)
        target_compile_definitions(kissfft::kissfft INTERFACE BEATMATE_USE_JUCE_FFT=1)
    endif()
endif()

# ============================================================
# TagLib - Audio metadata (v1.13.1 - stable, no FetchContent install issue)
# ============================================================
FetchContent_Declare(taglib
    GIT_REPOSITORY https://github.com/taglib/taglib.git
    GIT_TAG        v1.13.1
)
# Build TagLib statically without leaking BUILD_SHARED_LIBS=OFF into the parent
# scope (CMake 3.25+ block() gives us a private variable scope). On older CMake
# we fall back to save/restore. This was previously forcing BUILD_SHARED_LIBS
# globally, which silently turned every other dependency into a static lib.
if(POLICY CMP0140) # block() command available from CMake 3.25
    block(SCOPE_FOR VARIABLES)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
        set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(taglib)
    endblock()
else()
    set(_BM_PREV_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
    set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(taglib)
    set(BUILD_SHARED_LIBS "${_BM_PREV_BUILD_SHARED_LIBS}" CACHE BOOL "" FORCE)
    unset(_BM_PREV_BUILD_SHARED_LIBS)
endif()
if(NOT TARGET TagLib::TagLib)
    if(TARGET tag)
        add_library(TagLib::TagLib ALIAS tag)
    endif()
endif()
# Ensure TagLib include paths are available (use SYSTEM to suppress install prefix check)
if(TARGET tag)
    target_include_directories(tag SYSTEM PUBLIC
        ${taglib_SOURCE_DIR}
        ${taglib_SOURCE_DIR}/taglib
        ${taglib_SOURCE_DIR}/taglib/mpeg
        ${taglib_SOURCE_DIR}/taglib/mpeg/id3v2
        ${taglib_SOURCE_DIR}/taglib/mpeg/id3v2/frames
        ${taglib_SOURCE_DIR}/taglib/ogg
        ${taglib_SOURCE_DIR}/taglib/ogg/vorbis
        ${taglib_SOURCE_DIR}/taglib/flac
        ${taglib_SOURCE_DIR}/taglib/mp4
        ${taglib_SOURCE_DIR}/taglib/asf
        ${taglib_SOURCE_DIR}/taglib/riff
        ${taglib_SOURCE_DIR}/taglib/riff/wav
        ${taglib_SOURCE_DIR}/taglib/riff/aiff
        ${taglib_SOURCE_DIR}/taglib/toolkit
        ${taglib_BINARY_DIR}
    )
    # TagLib static linking: define TAGLIB_STATIC to avoid __declspec(dllimport)
    target_compile_definitions(tag PUBLIC TAGLIB_STATIC)
    # Suppress CMake install check for TagLib build dir paths
    set_target_properties(tag PROPERTIES EXPORT_NO_SYSTEM OFF)
    message(STATUS "TagLib: ENABLED (reading BPM, Key, Artist from ID3 tags)")
else()
    message(STATUS "TagLib: FAILED TO BUILD - using JUCE fallback for metadata")
    add_library(taglib_stub INTERFACE)
    target_compile_definitions(taglib_stub INTERFACE BEATMATE_NO_TAGLIB=1)
    add_library(TagLib::TagLib ALIAS taglib_stub)
endif()

# ============================================================
# nlohmann/json - JSON parsing (header-only)
# ============================================================
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)

# ============================================================
# spdlog - Fast structured logging
# ============================================================
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# ============================================================
# Signalsmith Stretch - Professional time-stretching (header-only)
# ============================================================
# Pinned to a fixed commit SHA (release tag 1.1.0) instead of `main` to keep the
# build reproducible across machines and CI runs. GIT_SHALLOW must stay OFF so
# CMake can resolve the SHA — shallow clones can only fetch named refs.
FetchContent_Declare(signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        44c8f865af9da8c29cc4a70a2d5a3ec83639c711
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(signalsmith_stretch)
if(signalsmith_stretch_POPULATED AND NOT TARGET signalsmith-stretch)
    add_library(signalsmith-stretch INTERFACE)
    target_include_directories(signalsmith-stretch INTERFACE
        ${signalsmith_stretch_SOURCE_DIR}
    )
endif()

# dr_libs not needed - JUCE handles all audio format reading natively

# ============================================================
# SQLite3MultipleCiphers - Drop-in SQLite with cipher support (Rekordbox 6/7 decryption)
# Provides identical SQLite3 API + PRAGMA cipher/key for SQLCipher-encrypted DBs
# ============================================================
FetchContent_Declare(sqlite3mc
    GIT_REPOSITORY https://github.com/utelle/SQLite3MultipleCiphers.git
    GIT_TAG        v2.1.1
)
set(SQLITE3MC_STATIC ON CACHE BOOL "" FORCE)
set(SQLITE3MC_BUILD_SHELL OFF CACHE BOOL "" FORCE)
set(SQLITE3MC_WITH_ICU OFF CACHE BOOL "" FORCE)
set(CODEC_TYPE "SQLCIPHER" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(sqlite3mc)

# Create 'sqlite3' alias that points to sqlite3mc for drop-in compatibility
if(TARGET sqlite3mc_static)
    add_library(sqlite3 ALIAS sqlite3mc_static)
    message(STATUS "SQLite: Using SQLite3MultipleCiphers (SQLCipher-compatible)")
elseif(TARGET sqlite3mc)
    add_library(sqlite3 ALIAS sqlite3mc)
    message(STATUS "SQLite: Using SQLite3MultipleCiphers (shared)")
endif()

# ============================================================
# ONNX Runtime - AI/ML inference (pre-built)
# CPU-only by default (~10 MB). Pass -DBEATMATE_USE_ONNX_GPU=ON to pull the
# CUDA/cuDNN flavoured build (~400 MB) when GPU inference is needed.
# ============================================================
set(ONNXRUNTIME_VERSION "1.18.0")
if(WIN32)
    if(BEATMATE_USE_ONNX_GPU)
        set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-win-x64-gpu-${ONNXRUNTIME_VERSION}.zip")
        message(STATUS "ONNX Runtime: GPU build (CUDA, ~400 MB)")
    else()
        set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-win-x64-${ONNXRUNTIME_VERSION}.zip")
        message(STATUS "ONNX Runtime: CPU-only build (~10 MB)")
    endif()
    FetchContent_Declare(onnxruntime
        URL ${ONNXRUNTIME_URL}
    )
    FetchContent_MakeAvailable(onnxruntime)
    if(onnxruntime_POPULATED)
        add_library(onnxruntime INTERFACE)
        target_include_directories(onnxruntime INTERFACE
            ${onnxruntime_SOURCE_DIR}/include
        )
        target_link_directories(onnxruntime INTERFACE
            ${onnxruntime_SOURCE_DIR}/lib
        )
        target_link_libraries(onnxruntime INTERFACE onnxruntime.lib)

        # Copy DLLs to output
        file(GLOB ONNXRUNTIME_DLLS "${onnxruntime_SOURCE_DIR}/lib/*.dll")
        if(TARGET BeatMateV11)
            foreach(DLL ${ONNXRUNTIME_DLLS})
                add_custom_command(TARGET BeatMateV11 POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${DLL} $<TARGET_FILE_DIR:BeatMateV11>)
            endforeach()
        endif()
    endif()
elseif(APPLE)
    set(BM_ONNX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
    file(GLOB BM_ONNX_VERSIONED "${BM_ONNX_DIR}/lib/libonnxruntime.[0-9]*.dylib")
    set(BM_ONNX_LINK "${BM_ONNX_DIR}/lib/libonnxruntime.dylib")
    if(NOT EXISTS "${BM_ONNX_LINK}" AND BM_ONNX_VERSIONED)
        list(GET BM_ONNX_VERSIONED 0 BM_ONNX_LINK)
    endif()
    if(EXISTS "${BM_ONNX_LINK}")
        message(STATUS "ONNX Runtime: macOS local (${BM_ONNX_DIR})")
        add_library(onnxruntime INTERFACE)
        target_include_directories(onnxruntime INTERFACE "${BM_ONNX_DIR}/include")
        target_link_libraries(onnxruntime INTERFACE "${BM_ONNX_LINK}")
        if(TARGET BeatMateV11 AND BM_ONNX_VERSIONED)
            add_custom_command(TARGET BeatMateV11 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${BM_ONNX_VERSIONED} $<TARGET_FILE_DIR:BeatMateV11>)
        endif()
    else()
        message(WARNING "ONNX Runtime macOS introuvable dans ${BM_ONNX_DIR}/lib")
    endif()
endif()

# ============================================================
# Catch2 - Testing framework (only for tests)
# ============================================================
if(BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

# ============================================================
# fmt - String formatting
# ============================================================
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        11.0.2
)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(fmt)

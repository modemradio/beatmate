# CompilerFlags.cmake - C++20 compiler flags and optimizations

if(MSVC)
    # MSVC flags
    add_compile_options(
        /W4             # Warning level 4
        /WX-            # Warnings not as errors (for now)
        /MP             # Multi-processor compilation
        /Zc:__cplusplus # Correct __cplusplus macro
        /Zc:preprocessor # Standards-conforming preprocessor
        /permissive-    # Standards conformance
        /utf-8          # Source and execution charset
        /EHsc           # Exception handling
        /bigobj         # Large object files support
    )

    # SIMD support
    add_compile_options(/arch:AVX2)

    # Release optimizations
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        add_compile_options(/O2 /Ob2 /Oi /GL)
        add_link_options(/LTCG /OPT:REF /OPT:ICF)
    endif()

    # Debug flags
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/Od /Zi /RTC1)
        add_compile_definitions(_DEBUG)
    endif()

    # Disable specific warnings
    add_compile_options(
        /wd4100  # unreferenced formal parameter
        /wd4244  # conversion, possible loss of data
        /wd4267  # size_t to int conversion
    )
else()
    # GCC/Clang flags
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )

    # [OVERLAY MAC] SIMD x86 (AVX2/FMA) invalides sur ARM64. Appliques a la
    # tranche x86_64 uniquement ; en universal2 via -Xarch_x86_64.
    set(_bm_has_x86 FALSE)
    set(_bm_universal FALSE)
    if(APPLE)
        if(CMAKE_OSX_ARCHITECTURES)
            if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
                set(_bm_has_x86 TRUE)
                if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
                    set(_bm_universal TRUE)
                endif()
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
            set(_bm_has_x86 TRUE)
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686")
        set(_bm_has_x86 TRUE)
    endif()
    if(_bm_universal)
        add_compile_options(-Xarch_x86_64 -mavx2 -Xarch_x86_64 -mfma)
    elseif(_bm_has_x86)
        add_compile_options(-mavx2 -mfma)
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG -flto)
        add_link_options(-flto)
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O0 -g -fsanitize=address,undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Platform definitions
if(WIN32)
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00  # Windows 10+
        UNICODE
        _UNICODE
    )
endif()

# Application definitions — version affichée au format simple (12.0.20).
add_compile_definitions(
    BEATMATE_VERSION="${PROJECT_VERSION}"
    BEATMATE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    BEATMATE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    BEATMATE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

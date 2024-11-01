cmake_minimum_required(VERSION 3.28)
project(Prescriptivism VERSION 0.1.0 LANGUAGES CXX C)

## ============================================================================
##  Global CMake Variables.
## ============================================================================
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## ============================================================================
##  Global compiler options.
## ============================================================================
## Turn on diagnostics colours.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fdiagnostics-color=always)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-fcolor-diagnostics)
endif()

## Use mold as the default linker, if it exists.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    find_program(MOLD_LINKER "mold")
    if (MOLD_LINKER)
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            add_compile_options(-fuse-ld=mold)
        endif()
        add_link_options(-fuse-ld=mold)
    endif()
endif()

## ============================================================================
##  Compiler options.
## ============================================================================
add_library(options INTERFACE)

## Flags for Clang and GCC.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(options INTERFACE
        ## Warnings.
        -Wall -Wextra     # Enable ‘all’ warnings.
        -Wundef           # Invalid #undef or undefined macro in #if.
        -Wcast-align      # Casting that changes alignment.
        -Wconversion      # Implicit conversions.
        -Wformat=2        # Stricter format checking.

        ## Disabled warnings.
        -Wno-sign-conversion
        -Wno-unused-function
        -Wno-unused-local-typedefs
        -Wno-implicit-int-float-conversion

        ## NULL Errors.
        -Werror=nonnull # Passing NULL to nonnull parameter.

        ## Memory Errors.
        -Werror=address              # Suspicious use of addresses.
        -Werror=init-self            # Initialization of a variable with itself.
        -Werror=uninitialized

        ## Return type.
        -Werror=return-type
        -Wmissing-noreturn

        ## C/C++.
        -Werror=implicit-fallthrough
        -Werror=pointer-arith        # Disallow void* and function pointer arithmetic.
        -Werror=string-compare       # Nonsensical string comparisons.
        -Werror=switch               # Missing switch cases.
        # -Werror=switch-enum          # Switch on enum (even if there is a default case).
        -Werror=write-strings        # Strings in C should be const char*.

        ## C++.
        -Werror=missing-field-initializers
        -Werror=non-virtual-dtor
        -Werror=pessimizing-move
    )
endif()

## Additional flags for GCC.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(options INTERFACE
        -Wlogical-op      # Duplicate or unintended logical operators.
        -Werror=invalid-memory-model # For atomics.
        -Werror=maybe-uninitialized
        -Werror=missing-requires
        -Werror=return-local-addr
    )
endif()

## Additional flags for Clang.
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(options INTERFACE
        -Werror=dangling
        -Werror=return-stack-address
    )
endif()

## Flags for MSVC.
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(options INTERFACE
        /W4 # Enable ‘all’ warnings.

        # Allow unnamed structs/unions.
        /wd4201

        # Source code is UTF-8.
        /utf-8
    )
endif()

## On Windows, don’t suggest the _s nonsense functions.
if (WIN32)
    target_compile_definitions(options INTERFACE
        _CRT_SECURE_NO_WARNINGS
        _CRT_SECURE_NO_WARNINGS_GLOBALS
        _CRT_NONSTDC_NO_WARNINGS
    )
endif()

## Address Sanitiser.
if (ENABLE_ASAN)
    target_compile_options(options INTERFACE -fsanitize=address)
    target_link_options(options INTERFACE -fsanitize=address)
endif()

## Debug/Release flags.
if (NOT MSVC)
    target_compile_options(options INTERFACE
        $<$<CONFIG:DEBUG>:-O0 -g3 -ggdb3>
        $<$<CONFIG:RELEASE>:-O3 -march=native>
    )
    target_link_options(options INTERFACE
        $<$<CONFIG:DEBUG>:-O0 -g3 -ggdb3 -rdynamic>
        $<$<CONFIG:RELEASE>:-O3 -march=native>
    )
else()
    target_compile_options(options INTERFACE
        $<$<CONFIG:DEBUG>:/Od>
        $<$<CONFIG:RELEASE>:/O2>
    )
endif()

## ============================================================================
##  Submodules and include dirs.
## ============================================================================
include(FetchContent)
FetchContent_Declare(base
    GIT_REPOSITORY https://github.com/Sirraide/libbase
    GIT_TAG master
)

FetchContent_Declare(sdl3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL
    GIT_TAG main
)

FetchContent_Declare(webp
    GIT_REPOSITORY https://github.com/webmproject/libwebp
    GIT_TAG v1.4.0
)

set(OPTION_BUILD_TOOLS OFF)
set(OPTION_BUILD_EXAMPLES OFF)
FetchContent_Declare(glbinding
    GIT_REPOSITORY https://github.com/cginternals/glbinding
    GIT_TAG v3.3.0
)

FetchContent_Declare(freetype
    GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype
    GIT_TAG VER-2-13-3
)

FetchContent_Declare(harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz
    GIT_TAG 10.0.1
)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm
    GIT_TAG 1.0.1
)

FetchContent_MakeAvailable(base sdl3 webp glbinding freetype harfbuzz glm)

## ============================================================================
##  Shared
## ============================================================================
file(GLOB_RECURSE common_sources src/Shared/*.cc)
file(GLOB_RECURSE common_modules lib/Shared/*.ccm)

add_library(PrescriptivismShared STATIC ${common_sources})
target_link_libraries(PrescriptivismShared PUBLIC options libbase)
target_sources(PrescriptivismShared PUBLIC FILE_SET CXX_MODULES FILES ${common_modules})

## ============================================================================
##  Server
## ============================================================================
file(GLOB_RECURSE server_sources src/Server/*.cc)
file(GLOB_RECURSE server_modules lib/Server/*.ccm)

add_executable(PrescriptivismServer ${server_sources})
target_sources(PrescriptivismServer PUBLIC FILE_SET CXX_MODULES FILES ${server_modules})
target_link_libraries(PrescriptivismServer PRIVATE
    PrescriptivismShared
)

## ============================================================================
##  Client
## ============================================================================
file(GLOB_RECURSE client_sources src/Client/*.cc)
file(GLOB_RECURSE client_modules lib/Client/*.ccm)

add_executable(Prescriptivism ${client_sources})
target_sources(Prescriptivism PUBLIC FILE_SET CXX_MODULES FILES ${client_modules})
target_link_libraries(Prescriptivism PRIVATE
    PrescriptivismShared
    SDL3::SDL3
    glbinding::glbinding
    glbinding::glbinding-aux
    glm::glm
    harfbuzz
    freetype
    webp
)

target_compile_options(Prescriptivism PRIVATE
    "--embed-dir=${PROJECT_SOURCE_DIR}/assets"
    -Wno-c23-extensions
)

target_include_directories(Prescriptivism PRIVATE
    ${PROJECT_SOURCE_DIR}/include/Client
)

## Allow overriding the default font w/o having to make it part of the project;
## the path must be absolute or relative to the assets directory.
if (NOT DEFINED PRESCRIPTIVISM_DEFAULT_FONT_PATH)
    set(PRESCRIPTIVISM_DEFAULT_FONT_PATH "NotoSans-Medium.ttf")
endif()

target_compile_definitions(Prescriptivism PRIVATE
    "PRESCRIPTIVISM_DEFAULT_FONT_PATH=\"${PRESCRIPTIVISM_DEFAULT_FONT_PATH}\""
)

## ============================================================================
##  Shared Properties
## ============================================================================
set_target_properties(PrescriptivismServer Prescriptivism PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

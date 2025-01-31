cmake_minimum_required(VERSION 3.28)
project(Prescriptivism VERSION 0.1.0 LANGUAGES CXX C)

if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Prescriptivism must be built with Clang 19 or later. Other compilers are not supported.")
endif()

## ============================================================================
##  Global CMake Variables.
## ============================================================================
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## ============================================================================
##  Global compiler options.
## ============================================================================
## Turn on diagnostics colours.
add_compile_options(-fcolor-diagnostics)

## Use mold as the default linker, if it exists.
find_program(MOLD_LINKER "mold")
if (MOLD_LINKER)
    add_link_options(-fuse-ld=mold)
endif()

## ============================================================================
##  Compiler options.
## ============================================================================
set(LIBBASE_CXXFLAGS "-fno-exceptions")
add_library(options INTERFACE)
target_compile_options(options INTERFACE
    -fms-extensions
    -fdeclspec
    -ftemplate-backtrace-limit=0
    -fno-exceptions

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
    -Wno-c99-designator

    ## NULL Errors.
    -Werror=nonnull # Passing NULL to nonnull parameter.

    ## Memory Errors.
    -Werror=address              # Suspicious use of addresses.
    -Werror=init-self            # Initialization of a variable with itself.
    -Werror=uninitialized
    -Werror=return-stack-address
    -Werror=dangling

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

    ## Optimisation flags.
    $<$<CONFIG:DEBUG>:-O0 -g3 -ggdb3>
    $<$<CONFIG:RELEASE>:-O3 -march=native>
)

target_link_options(options INTERFACE
    $<$<CONFIG:DEBUG>:-O0 -g3 -ggdb3 -rdynamic>
    $<$<CONFIG:RELEASE>:-O3 -march=native>
)

if (PRESCRIPTIVISM_ENABLE_SANITISERS)
    target_compile_options(options INTERFACE
        -fsanitize=undefined,address
    )

    target_link_options(options INTERFACE
        -fsanitize=undefined,address
    )

    target_compile_definitions(options INTERFACE
        -DPRESCRIPTIVISM_ENABLE_SANITISERS=1
    )
endif()

target_include_directories(options INTERFACE
    "${PROJECT_SOURCE_DIR}/include"
)

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

## Disable this warning in third-party dependencies.
file(GLOB_RECURSE prescriptivism_deps
    "${CMAKE_CURRENT_BINARY_DIR}/*.h"
    "${CMAKE_CURRENT_BINARY_DIR}/*.hh"
)

set_source_files_properties(
    ${prescriptivism_deps}
    PROPERTIES
    COMPILE_FLAGS "-Wno-nontrivial-memcall"
)

## ============================================================================
##  Tools
## ============================================================================
add_executable(runner tools/DevRunner.cc)
target_link_libraries(runner PRIVATE options libbase)
set_target_properties(runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_NAME "start"
)


## ============================================================================
##  Shared
## ============================================================================
file(GLOB_RECURSE common_sources src/Shared/*.cc)
file(GLOB_RECURSE common_headers include/Shared/*.hh)
add_library(PrescriptivismShared STATIC ${common_sources})
target_link_libraries(PrescriptivismShared PUBLIC options libbase)
target_sources(PrescriptivismShared PUBLIC FILE_SET HEADERS FILES ${common_headers})

## ============================================================================
##  Server
## ============================================================================
file(GLOB_RECURSE server_sources src/Server/*.cc)
file(GLOB_RECURSE server_headers include/Server/*.hh)

add_executable(PrescriptivismServer ${server_sources})
target_sources(PrescriptivismServer PUBLIC FILE_SET HEADERS FILES ${server_headers})
target_link_libraries(PrescriptivismServer PRIVATE
    PrescriptivismShared
)

## ============================================================================
##  Client
## ============================================================================
file(GLOB_RECURSE client_sources src/Client/*.cc)
file(GLOB_RECURSE client_headers include/Client/*.hh)

add_executable(Prescriptivism ${client_sources})
target_sources(Prescriptivism PUBLIC FILE_SET HEADERS FILES ${client_headers})
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

## Allow overriding the default font w/o having to make it part of the project;
## the path must be absolute or relative to the assets directory.
if (NOT DEFINED PRESCRIPTIVISM_DEFAULT_FONT_PATH)
    set(PRESCRIPTIVISM_DEFAULT_FONT_PATH "NotoSans-Medium.ttf")
endif()

target_compile_definitions(Prescriptivism PRIVATE
    "PRESCRIPTIVISM_DEFAULT_FONT_PATH=\"${PRESCRIPTIVISM_DEFAULT_FONT_PATH}\""
)

set_target_properties(Prescriptivism PROPERTIES
    WIN32_EXECUTABLE ON
)

## ============================================================================
##  Shared Properties
## ============================================================================
set_target_properties(PrescriptivismServer Prescriptivism PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

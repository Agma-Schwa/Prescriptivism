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

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG v1.91.4
    SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/libs/imgui"
)

FetchContent_MakeAvailable(base sdl3 webp glbinding imgui)

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

## ImGui doesn’t use CMake for some ungodly reason, so
## we’ve added it as a submodule. It also just doesn’t
## work to compile this as a separate library, so we’re
## just going to add the source files to the main exe.
file(GLOB imgui_sources
    "${CMAKE_CURRENT_BINARY_DIR}/libs/imgui/*.h"
    "${CMAKE_CURRENT_BINARY_DIR}/libs/imgui/*.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/libs/imgui/backends/imgui_impl_sdl3.h"
    "${CMAKE_CURRENT_BINARY_DIR}/libs/imgui/backends/imgui_impl_sdl3.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/libs/imgui/backends/imgui_impl_opengl3.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/libs/imgui/backends/imgui_impl_opengl3.h"
)

set_source_files_properties(${imgui_sources}
    PROPERTIES
    COMPILE_FLAGS
    "-Wno-conversion -Wno-sign-conversion -Wno-implicit-int-float-conversion"
)

set_source_files_properties(libs/Client/impl.c
    PROPERTIES
    COMPILE_FLAGS
    -Wno-error=implicit-fallthrough
)

add_executable(Prescriptivism ${client_sources} ${imgui_sources})
target_sources(Prescriptivism PUBLIC FILE_SET CXX_MODULES FILES ${client_modules})
target_link_libraries(Prescriptivism PRIVATE
    PrescriptivismShared
    SDL3::SDL3
    glbinding::glbinding
    glbinding::glbinding-aux
    webp
)

target_compile_options(Prescriptivism PRIVATE
    "--embed-dir=${PROJECT_SOURCE_DIR}/assets"
    -Wno-c23-extensions
)

target_include_directories(Prescriptivism PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/libs/imgui"
    "${CMAKE_CURRENT_BINARY_DIR}/libs/imgui/backends"
    "${PROJECT_SOURCE_DIR}/libs/Client"
)

## ============================================================================
##  Shared Properties
## ============================================================================
set_target_properties(PrescriptivismServer Prescriptivism PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

# SPDX-FileCopyrightText: 2026 Kea contributors
# SPDX-License-Identifier: MIT
#
# Fetch and build parakeet.cpp (https://github.com/mudler/parakeet.cpp) as two
# shared libraries with an identical flat C-API surface, so Kea can switch the
# inference backend (CPU vs Vulkan) at runtime via dlopen.
#
# The fetch happens at *build* time (`cmake --build`), not configure time, via
# ExternalProject_Add. Each variant is a fully isolated CMake invocation of the
# upstream project (its own clone + build dir), which avoids target-name and
# stamp collisions between the CPU and Vulkan builds. ggml (parakeet.cpp's only
# submodule) is pulled recursively and is statically linked into each .so, so
# the resulting libparakeet.so is self-contained for dlopen.
#
# Upstream has no `install()` rules, so the .so stays in the variant build dir
# (INSTALL_COMMAND is a no-op). The dlopen loader resolves these paths at
# runtime via the KEA_PARAKEET_*_LIB defines baked in below.
#
# Produced artifacts (under the build tree):
#   ${CMAKE_BINARY_DIR}/parakeet/cpu/libparakeet.so
#   ${CMAKE_BINARY_DIR}/parakeet/vulkan/libparakeet.so
#
# Disabled when KEA_BUILD_PARAKEET=OFF, so the GUI can be developed without the
# (slow) ASR build.

include(ExternalProject)

# Defaults: empty when the parakeet build is off / a variant is skipped.
set(KEA_PARAKEET_CPU_LIB "")
set(KEA_PARAKEET_VULKAN_LIB "")

function(_kea_parakeet_add_variant name out_so_path)
    set(options VULKAN)
    cmake_parse_arguments(ARG "${options}" "" "" ${ARGN})

    set(_src  "${CMAKE_BINARY_DIR}/parakeet/${name}-src")
    set(_bld  "${CMAKE_BINARY_DIR}/parakeet/${name}-build")
    set(_vk_flag -DPARAKEET_GGML_VULKAN=OFF)
    if(ARG_VULKAN)
        set(_vk_flag -DPARAKEET_GGML_VULKAN=ON)
    endif()

    ExternalProject_Add(parakeet_${name}
        GIT_REPOSITORY       "${KEA_PARAKEET_GIT_REPOSITORY}"
        GIT_TAG              "${KEA_PARAKEET_GIT_TAG}"
        GIT_SHALLOW          TRUE
        GIT_SUBMODULES_RECURSE TRUE
        SOURCE_DIR           "${_src}"
        BINARY_DIR           "${_bld}"
        CMAKE_GENERATOR      "${CMAKE_GENERATOR}"
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_CXX_STANDARD=17
            -DPARAKEET_BUILD_TESTS=OFF
            -DPARAKEET_BUILD_CLI=OFF
            -DPARAKEET_BUILD_SERVER=OFF
            -DPARAKEET_SHARED=ON
            ${_vk_flag}
        # Upstream has no install rules; the .so is in BINARY_DIR.
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -S "${_src}" -B "${_bld}"
                          -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                          -DCMAKE_CXX_STANDARD=17
                          -DPARAKEET_BUILD_TESTS=OFF
                          -DPARAKEET_BUILD_CLI=OFF
                          -DPARAKEET_BUILD_SERVER=OFF
                          -DPARAKEET_SHARED=ON
                          ${_vk_flag}
        BUILD_COMMAND     ${CMAKE_COMMAND} --build "${_bld}" --config ${CMAKE_BUILD_TYPE}
        BUILD_BYPRODUCTS  "${_bld}/libparakeet.so"
        INSTALL_COMMAND   ""
        TEST_COMMAND      ""
    )

    set(${out_so_path} "${_bld}/libparakeet.so" PARENT_SCOPE)
endfunction()

if(KEA_BUILD_PARAKEET)

    _kea_parakeet_add_variant(cpu  KEA_PARAKEET_CPU_LIB)

    find_package(Vulkan)
    if(Vulkan_FOUND)
        _kea_parakeet_add_variant(vulkan KEA_PARAKEET_VULKAN_LIB VULKAN)
    endif()

    # Aggregate target so `cmake --build . --target parakeet_all` builds every
    # variant at once. DEPENDS uses a plain list (not a generator expression):
    # whether the Vulkan variant exists is known at configure time.
    set(_kea_parakeet_deps parakeet_cpu)
    if(Vulkan_FOUND)
        list(APPEND _kea_parakeet_deps parakeet_vulkan)
    endif()
    add_custom_target(parakeet_all ALL
        DEPENDS ${_kea_parakeet_deps}
        COMMENT "parakeet.cpp backends built"
    )

    message(STATUS "parakeet.cpp: will be fetched at build time from ${KEA_PARAKEET_GIT_REPOSITORY}@${KEA_PARAKEET_GIT_TAG}")
    message(STATUS "parakeet.cpp CPU variant:    ${KEA_PARAKEET_CPU_LIB}")
    if(Vulkan_FOUND)
        message(STATUS "parakeet.cpp Vulkan variant: ${KEA_PARAKEET_VULKAN_LIB}")
    else()
        message(STATUS "parakeet.cpp Vulkan variant: skipped (Vulkan not found)")
    endif()

endif()

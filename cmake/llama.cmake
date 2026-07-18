# SPDX-FileCopyrightText: 2026 Kea contributors
# SPDX-License-Identifier: MIT
#
# Fetch and build llama.cpp (https://github.com/ggml-org/llama.cpp) as a shared
# library for on-device LLM post-processing (issue #6). Optional: disable with
# -DKEA_BUILD_LLAMA=OFF for fast GUI-only builds.
#
# Artifacts:
#   ${CMAKE_BINARY_DIR}/llama/build/bin/libllama.so  (and libggml*.so deps)
#   Headers under ${CMAKE_BINARY_DIR}/llama/src/...

include(ExternalProject)

set(KEA_LLAMA_INCLUDE_DIR "")
set(KEA_LLAMA_LIB "")
set(KEA_HAS_LLAMA_DEFINE "")

if(KEA_BUILD_LLAMA)
    set(_llama_src "${CMAKE_BINARY_DIR}/llama/src")
    set(_llama_bld "${CMAKE_BINARY_DIR}/llama/build")

    ExternalProject_Add(llama_cpp
        GIT_REPOSITORY       "${KEA_LLAMA_GIT_REPOSITORY}"
        GIT_TAG              "${KEA_LLAMA_GIT_TAG}"
        GIT_SHALLOW          TRUE
        UPDATE_COMMAND       ""
        SOURCE_DIR           "${_llama_src}"
        BINARY_DIR           "${_llama_bld}"
        CMAKE_GENERATOR      "${CMAKE_GENERATOR}"
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -S "${_llama_src}" -B "${_llama_bld}"
                          -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                          -DCMAKE_CXX_STANDARD=17
                          -DBUILD_SHARED_LIBS=ON
                          -DLLAMA_BUILD_TESTS=OFF
                          -DLLAMA_BUILD_TOOLS=OFF
                          -DLLAMA_BUILD_EXAMPLES=OFF
                          -DLLAMA_BUILD_SERVER=OFF
                          -DLLAMA_CURL=OFF
                          -DGGML_NATIVE=OFF
                          -DGGML_VULKAN=OFF
                          -DGGML_CUDA=OFF
        BUILD_COMMAND     ${CMAKE_COMMAND} --build "${_llama_bld}" --config ${CMAKE_BUILD_TYPE}
        # libllama.so location varies slightly by version (bin/ vs build root).
        BUILD_BYPRODUCTS  "${_llama_bld}/bin/libllama.so"
                          "${_llama_bld}/libllama.so"
        INSTALL_COMMAND   ""
        TEST_COMMAND      ""
    )

    # Prefer bin/ layout used by recent llama.cpp; fallback to build root.
    if(EXISTS "${_llama_bld}/bin/libllama.so")
        set(KEA_LLAMA_LIB "${_llama_bld}/bin/libllama.so")
    else()
        set(KEA_LLAMA_LIB "${_llama_bld}/bin/libllama.so")
    endif()
    set(KEA_LLAMA_INCLUDE_DIR "${_llama_src}/include")
    set(KEA_HAS_LLAMA_DEFINE "KEA_HAS_LLAMA=1")

    message(STATUS "llama.cpp: will fetch ${KEA_LLAMA_GIT_TAG} into ${_llama_src}")
    message(STATUS "llama.cpp lib (expected): ${KEA_LLAMA_LIB}")
else()
    message(STATUS "llama.cpp: skipped (KEA_BUILD_LLAMA=OFF)")
endif()

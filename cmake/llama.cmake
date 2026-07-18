# SPDX-FileCopyrightText: 2026 Kea contributors
# SPDX-License-Identifier: MIT
#
# Fetch and build llama.cpp (https://github.com/ggml-org/llama.cpp) as a shared
# library for on-device LLM post-processing (issue #6). Optional: disable with
# -DKEA_BUILD_LLAMA=OFF for fast GUI-only builds.
#
# Only the `llama` library target is built (not tools/app/server). Artifacts:
#   ${CMAKE_BINARY_DIR}/llama/build/bin/libllama.so (+ libggml*.so deps)
#   Headers: ${CMAKE_BINARY_DIR}/llama/src/include/llama.h

include(ExternalProject)

set(KEA_LLAMA_INCLUDE_DIR "")
set(KEA_LLAMA_LIB_DIR "")
set(KEA_LLAMA_LIB "")

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
        # Explicit OFF for every consumer target: LLAMA_STANDALONE defaults them
        # ON when llama.cpp is the top-level project of the ExternalProject, and
        # building llama-app fails without a full tools/common install layout.
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -S "${_llama_src}" -B "${_llama_bld}"
                          -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                          -DCMAKE_CXX_STANDARD=17
                          -DBUILD_SHARED_LIBS=ON
                          -DLLAMA_BUILD_COMMON=OFF
                          -DLLAMA_BUILD_TESTS=OFF
                          -DLLAMA_BUILD_TOOLS=OFF
                          -DLLAMA_BUILD_EXAMPLES=OFF
                          -DLLAMA_BUILD_SERVER=OFF
                          -DLLAMA_BUILD_APP=OFF
                          -DLLAMA_BUILD_UI=OFF
                          -DLLAMA_CURL=OFF
                          -DLLAMA_OPENSSL=OFF
                          -DGGML_NATIVE=OFF
                          -DGGML_VULKAN=OFF
                          -DGGML_CUDA=OFF
        # Build only the library — never the app/tools that depend on common.
        BUILD_COMMAND     ${CMAKE_COMMAND} --build "${_llama_bld}" --config ${CMAKE_BUILD_TYPE}
                                          --target llama -j
        BUILD_BYPRODUCTS  "${_llama_bld}/bin/libllama.so"
                          "${_llama_bld}/libllama.so"
        INSTALL_COMMAND   ""
        TEST_COMMAND      ""
    )

    set(KEA_LLAMA_INCLUDE_DIR "${_llama_src}/include")
    set(KEA_LLAMA_LIB_DIR "${_llama_bld}/bin")
    set(KEA_LLAMA_LIB "${_llama_bld}/bin/libllama.so")

    message(STATUS "llama.cpp: will fetch ${KEA_LLAMA_GIT_TAG} into ${_llama_src}")
    message(STATUS "llama.cpp lib (expected): ${KEA_LLAMA_LIB}")
else()
    message(STATUS "llama.cpp: skipped (KEA_BUILD_LLAMA=OFF)")
endif()

# --- Kea QML module + executable (the settings window / tray host) ---
add_executable(kea)
ecm_add_qml_module(kea
    URI io.github.su3h7am.kea
    VERSION 1.0
)

target_sources(kea
    PRIVATE
        main.cpp
        logging.cpp
        logging.h
        app/tray_controller.h
        app/tray_controller.cpp
        app/app_settings.h
        app/app_settings.cpp
        app/readiness.h
        app/readiness.cpp
        app/model_downloader.h
        app/model_downloader.cpp
        hotkey/global_hotkey.h
        hotkey/global_hotkey.cpp
        controller/parakeet_worker.h
        controller/parakeet_worker.cpp
        controller/dictation_controller.h
        controller/dictation_controller.cpp
)

ecm_target_qml_sources(kea
    SOURCES
        app/Main.qml
)

target_link_libraries(kea
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::Network
        Qt6::WaylandClient
        KF6::I18n
        KF6::CoreAddons
        KF6::IconThemes
        KF6::GlobalAccel
        KF6::StatusNotifierItem
        kea_audio
        kea_inference
        kea_insert
)

install(TARGETS kea ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# --- parakeet.cpp: fetch + build the two .so variants (CPU + Vulkan) ---
# Sets KEA_PARAKEET_CPU_LIB / KEA_PARAKEET_VULKAN_LIB (empty when not built).
include(${CMAKE_SOURCE_DIR}/cmake/parakeet.cmake)

# --- DSP lib: resampler + WAV loader (no Qt, no model needed). ---
add_library(kea_dsp STATIC
    audio/resampler.h
    audio/resampler.cpp
    audio/wav_loader.h
    audio/wav_loader.cpp
)
target_include_directories(kea_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- Audio capture lib: QAudioSource (PipeWire) wrapper, links Multimedia. ---
add_library(kea_audio STATIC
    audio/audio_recorder.h
    audio/audio_recorder.cpp
)
target_include_directories(kea_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kea_audio
    PUBLIC
        Qt6::Core
        kea_dsp
    PRIVATE
        Qt6::Multimedia
)

# --- Resampler unit test (headless, pure DSP). ---
# enable_testing() lives in the top-level CMakeLists.txt so `ctest` works from
# the build root regardless of which subdirectory happens to add the first test.
add_executable(kea-resampler-test resampler_test.cpp)
target_link_libraries(kea-resampler-test PRIVATE kea_dsp)
add_test(NAME resampler COMMAND kea-resampler-test)

# --- Text insertion: input-method-v1 client + serial-safe committer. ---
# KWin implements input_method_unstable_v1 (not v2). Protocol XML is vendored
# under data/protocols/; qtwaylandscanner generates the client wrappers.
add_library(kea_insert STATIC
    insert/input_context.h
    insert/input_method.h
    insert/input_method.cpp
    insert/text_committer.h
    insert/text_committer.cpp
    insert/insertion_router.h
    insert/insertion_router.cpp
)
target_include_directories(kea_insert PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kea_insert
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::WaylandClient
    PRIVATE
        Wayland::Client
)
# NO_INCLUDE_CORE_ONLY: this protocol references wl_keyboard (grab_keyboard),
# which needs the full wayland-client core headers, not the core-only stub.
qt_generate_wayland_protocol_client_sources(kea_insert
    NO_INCLUDE_CORE_ONLY
    FILES
        ${CMAKE_SOURCE_DIR}/data/protocols/input-method-unstable-v1.xml
)

# Headless unit test for TextCommitter (mock IInputContext, no compositor).
add_executable(kea-committer-test committer_test.cpp)
target_link_libraries(kea-committer-test PRIVATE Qt6::Core kea_insert)
add_test(NAME committer COMMAND kea-committer-test)

# InsertionRouter: IM path + clipboard fallback (needs QGuiApplication clipboard).
add_executable(kea-insertion-router-test insertion_router_test.cpp)
target_link_libraries(kea-insertion-router-test PRIVATE Qt6::Core Qt6::Gui kea_insert)
add_test(NAME insertion-router COMMAND kea-insertion-router-test)

# Headless controller smoke (missing model → Error).
add_executable(kea-controller-test controller_test.cpp
    logging.cpp
    app/app_settings.cpp
    controller/dictation_controller.cpp
    controller/parakeet_worker.cpp
    hotkey/global_hotkey.cpp
)
target_link_libraries(kea-controller-test
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Multimedia
        KF6::GlobalAccel
        KF6::I18n
        kea_audio
        kea_inference
        kea_insert
)
add_test(NAME controller COMMAND kea-controller-test)

# --- Inference static lib: the dlopen loader. ---
# Compiles without the upstream header (opaque types declared in the .cpp), so
# it builds even when KEA_BUILD_PARAKEET=OFF. The path defines resolve to the
# empty string for variants that weren't built; the loader reports that at
# runtime.
add_library(kea_inference STATIC
    inference/parakeet_backend.h
    inference/parakeet_backend.cpp
)
target_link_libraries(kea_inference
    PUBLIC
        Qt6::Core
    PRIVATE
        ${CMAKE_DL_LIBS}
)
target_compile_definitions(kea_inference PRIVATE
    KEA_PARAKEET_CPU_LIB="${KEA_PARAKEET_CPU_LIB}"
    KEA_PARAKEET_VULKAN_LIB="${KEA_PARAKEET_VULKAN_LIB}"
)

# --- Fake parakeet C-API shared library (test-only double for ParakeetBackend). ---
# Mimics the upstream flat C-API surface so ParakeetBackend's dlopen loader
# can be exercised without fetching/building the real (heavy,
# network-dependent) parakeet.cpp. See inference/fake_parakeet_capi.cpp for
# the subset of the upstream contract implemented by the test double.
add_library(kea-fake-parakeet-capi SHARED
    inference/fake_parakeet_capi.cpp
)
set_target_properties(kea-fake-parakeet-capi PROPERTIES
    CXX_VISIBILITY_PRESET default
)

# Headless unit test for ParakeetBackend's loader/loadModel error handling.
add_executable(kea-parakeet-backend-test parakeet_backend_test.cpp)
target_link_libraries(kea-parakeet-backend-test PRIVATE Qt6::Core kea_inference)
add_dependencies(kea-parakeet-backend-test kea-fake-parakeet-capi)
target_compile_definitions(kea-parakeet-backend-test PRIVATE
    KEA_FAKE_PARAKEET_LIB="$<TARGET_FILE:kea-fake-parakeet-capi>")
add_test(NAME parakeet_backend COMMAND kea-parakeet-backend-test)

# --- Phase 0 smoke test: load a backend, transcribe a WAV. ---
# Only when the parakeet backends are actually built (it needs the .so files).
if(KEA_BUILD_PARAKEET)
    add_executable(kea-parakeet-smoke parakeet_smoke.cpp)
    target_link_libraries(kea-parakeet-smoke PRIVATE Qt6::Core kea_inference)
    add_dependencies(kea-parakeet-smoke parakeet_all)
    install(TARGETS kea-parakeet-smoke ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

    # --- Phase 1 smoke test: streaming pipeline (WAV -> 16k -> stream -> text). ---
    add_executable(kea-stream-smoke stream_smoke.cpp)
    target_link_libraries(kea-stream-smoke PRIVATE Qt6::Core kea_dsp kea_inference)
    add_dependencies(kea-stream-smoke parakeet_all)
    install(TARGETS kea-stream-smoke ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
endif()

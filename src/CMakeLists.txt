# --- Kea QML module + executable (the settings window / tray host) ---
add_executable(kea)
ecm_add_qml_module(kea
    URI org.kde.kea
    VERSION 1.0
)

target_sources(kea
    PRIVATE
        main.cpp
        app/tray_controller.h
        app/tray_controller.cpp
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
        Qt6::WaylandClient
        KF6::I18n
        KF6::CoreAddons
        KF6::IconThemes
        KF6::GlobalAccel
        KF6::StatusNotifierItem
)

install(TARGETS kea ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# --- parakeet.cpp: fetch + build the two .so variants (CPU + Vulkan) ---
# Sets KEA_PARAKEET_CPU_LIB / KEA_PARAKEET_VULKAN_LIB (empty when not built).
include(${CMAKE_SOURCE_DIR}/cmake/parakeet.cmake)

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

# --- Phase 0 smoke test: load a backend, transcribe a WAV. ---
# Only when the parakeet backends are actually built (it needs the .so files).
if(KEA_BUILD_PARAKEET)
    add_executable(kea-parakeet-smoke parakeet_smoke.cpp)
    target_link_libraries(kea-parakeet-smoke PRIVATE Qt6::Core kea_inference)
    add_dependencies(kea-parakeet-smoke parakeet_all)
    install(TARGETS kea-parakeet-smoke ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
endif()

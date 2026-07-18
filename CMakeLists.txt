cmake_minimum_required(VERSION 3.21)

project(kea
    VERSION 0.1.0
    DESCRIPTION "On-device, streaming voice dictation for KDE Plasma (Wayland)"
    LANGUAGES C CXX
)

# --- KDE/Qt build plumbing ---
find_package(ECM 6.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMFindQmlModule)
include(ECMQmlModule)
include(FeatureSummary)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# So `ctest` works from the build root regardless of which subdirectory adds
# the first test (was previously called from inside src/CMakeLists.txt).
enable_testing()

# --- Qt6 ---
find_package(Qt6 6.6 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Widgets
    Multimedia
    Network
    WaylandClient
)

# --- KDE Frameworks 6 ---
find_package(KF6 REQUIRED COMPONENTS
    Kirigami
    I18n
    CoreAddons
    Config
    IconThemes
    GlobalAccel
    StatusNotifierItem
)

# Wayland protocol code generation (input-method-unstable-v1 for text insertion).
find_package(Wayland REQUIRED COMPONENTS Client)
find_package(WaylandScanner REQUIRED)
find_package(Qt6WaylandScannerTools REQUIRED)

# QtQuick Controls style: Breeze desktop style (matches Plasma).
ecm_find_qmlmodule(org.kde.kirigami REQUIRED)
# Key sequence capture widget (System Settings–style shortcut editor).
ecm_find_qmlmodule(org.kde.kquickcontrols REQUIRED)

# --- parakeet.cpp: fetched and built by ExternalProject (two variants) ---
# Controlled so the GUI can build independently of the heavy ASR build.
option(KEA_BUILD_PARAKEET "Fetch and build parakeet.cpp (CPU + Vulkan variants) at build time" ON)
set(KEA_PARAKEET_GIT_REPOSITORY "https://github.com/mudler/parakeet.cpp" CACHE STRING "parakeet.cpp git source")
# Pinned to a known-good commit rather than a floating branch: "master" made
# every from-scratch build non-reproducible and liable to break without any
# change on Kea's side. Bump deliberately (and re-test) when picking up
# upstream changes.
set(KEA_PARAKEET_GIT_TAG "1da853421de9710cbe894a0110711de5a0516486" CACHE STRING "parakeet.cpp git ref to fetch")

add_subdirectory(src)

install(PROGRAMS io.github.su3h7am.kea.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES io.github.su3h7am.kea.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES data/models.json DESTINATION ${KDE_INSTALL_DATADIR}/kea)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

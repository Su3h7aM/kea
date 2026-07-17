cmake_minimum_required(VERSION 3.21)

project(kea
    VERSION 0.1.0
    DESCRIPTION "On-device, streaming voice dictation for KDE Plasma (Wayland)"
    LANGUAGES CXX
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

# --- Qt6 ---
find_package(Qt6 6.6 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Widgets
    Multimedia
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

# QtQuick Controls style: Breeze desktop style (matches Plasma).
ecm_find_qmlmodule(org.kde.kirigami REQUIRED)

# --- parakeet.cpp: fetched and built by ExternalProject (two variants) ---
# Controlled so the GUI can build independently of the heavy ASR build.
option(KEA_BUILD_PARAKEET "Fetch and build parakeet.cpp (CPU + Vulkan variants) at build time" ON)
set(KEA_PARAKEET_GIT_REPOSITORY "https://github.com/mudler/parakeet.cpp" CACHE STRING "parakeet.cpp git source")
set(KEA_PARAKEET_GIT_TAG "master" CACHE STRING "parakeet.cpp git ref to fetch")

add_subdirectory(src)

install(PROGRAMS org.kde.kea.desktop DESTINATION ${KDE_INSTALL_APPDIR})

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

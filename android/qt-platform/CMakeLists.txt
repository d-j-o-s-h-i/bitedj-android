cmake_minimum_required(VERSION 3.21)
project(mixxx_android_qt_platform LANGUAGES CXX)

# Rebuild only the matching Qt platform plugin against the pinned Qt binaries.
# Input and raster-flush coordinate patches are applied by build-qt-platform.py.
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui)
if(NOT ANDROID OR NOT QTBASE_SOURCE_DIR)
    message(FATAL_ERROR "Requires Android and the verified Qt 6.8.3 sources")
endif()
set(PLATFORM_DIR "${QTBASE_SOURCE_DIR}/src/plugins/platforms/android")
file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "${PLATFORM_DIR}/*.cpp" "${PLATFORM_DIR}/*.h")
# Style extraction is disabled in the official Android binary distribution.
list(REMOVE_ITEM PLATFORM_SOURCES "${PLATFORM_DIR}/extract.cpp")
add_library(mixxx_qt_android_platform MODULE ${PLATFORM_SOURCES})
set_target_properties(mixxx_qt_android_platform PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    PREFIX "libplugins_platforms_"
    OUTPUT_NAME "qtforandroid_arm64-v8a")
target_compile_definitions(mixxx_qt_android_platform PRIVATE QT_PLUGIN QT_USE_QSTRINGBUILDER)
target_include_directories(mixxx_qt_android_platform PRIVATE
    "${PLATFORM_DIR}" "${QTBASE_SOURCE_DIR}/src/3rdparty/android"
    "${Qt6Gui_DIR}/../../../include/QtGui/6.8.3/QtGui/qpa")
target_link_libraries(mixxx_qt_android_platform PRIVATE
    Qt6::Core Qt6::CorePrivate Qt6::Gui Qt6::GuiPrivate android log jnigraphics EGL GLESv2)
target_link_options(mixxx_qt_android_platform PRIVATE "-Wl,-z,max-page-size=16384")

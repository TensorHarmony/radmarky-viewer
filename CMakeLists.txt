cmake_minimum_required(VERSION 3.25)

include(cmake/Version.cmake)

project(
    RadmarkyViewer
    VERSION ${RADMARKY_VERSION_BASE}
    DESCRIPTION "RadMarky Viewer, a lightweight Qt, ITK, and VTK medical imaging viewer"
    LANGUAGES CXX
)

include(CTest)
include(cmake/ClangTidy.cmake)
include(cmake/QtRuntime.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(ITK 5.4 CONFIG REQUIRED COMPONENTS
    ITKCommon
    ITKIOImageBase
    ITKIOGDCM
    ITKIONIFTI
)
include(${ITK_USE_FILE})

find_package(LibArchive REQUIRED)
find_package(GIF REQUIRED)
find_package(FFMPEG REQUIRED)
# Validators run in an installed CPython process. Only the interpreter is a
# build/runtime prerequisite; no Python headers, import library, or runtime tree
# are linked or packaged with RadMarky Viewer.
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
file(TO_CMAKE_PATH "${Python3_EXECUTABLE}" RADMARKY_DEFAULT_PYTHON_EXECUTABLE)
if(WIN32 AND TARGET LibArchive::LibArchive AND DEFINED VCPKG_INSTALLED_DIR)
    set(_radmarky_libarchive_prefix
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    set_target_properties(LibArchive::LibArchive PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG
            "${_radmarky_libarchive_prefix}/debug/lib/archive.lib"
        IMPORTED_LOCATION_RELEASE
            "${_radmarky_libarchive_prefix}/lib/archive.lib"
        MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
    )
    unset(_radmarky_libarchive_prefix)
endif()

# QVTKOpenGLNativeWidget requires these Qt modules with Qt 6.
find_package(Qt6 6.5 CONFIG REQUIRED COMPONENTS
    Concurrent
    Core
    Gui
    OpenGLWidgets
    Svg
    Widgets
)

# Keep this list limited to modules used by the orthogonal slice pipelines.
find_package(VTK CONFIG REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    FiltersSources
    GUISupportQt
    ImagingCore
    InteractionStyle
    RenderingCore
    RenderingOpenGL2
)

if(NOT TARGET VTK::GUISupportQt)
    message(FATAL_ERROR "VTK was found without Qt support. Install the vtk[qt] vcpkg feature.")
endif()

add_library(radmarky_app_info STATIC
    src/app/ApplicationInfo.cpp
    src/app/ApplicationInfo.h
)
target_include_directories(radmarky_app_info PUBLIC src)
target_compile_definitions(radmarky_app_info PRIVATE
    RADMARKY_VERSION="${RADMARKY_VERSION}"
    RADMARKY_RELEASE_DATE="${RADMARKY_RELEASE_DATE}"
)

add_library(radmarky_core STATIC
    src/core/Annotation.cpp
    src/core/Annotation.h
    src/core/AnnotationEditor.cpp
    src/core/AnnotationEditor.h
    src/core/BrushGeometry.cpp
    src/core/BrushGeometry.h
    src/core/ImageGeometry.cpp
    src/core/ImageGeometry.h
    src/core/LabelPalette.h
    src/core/OrthogonalSliceGeometry.cpp
    src/core/OrthogonalSliceGeometry.h
    src/core/PhysicalMeasurement.cpp
    src/core/PhysicalMeasurement.h
    src/core/ViewerState.cpp
    src/core/ViewerState.h
    src/core/Volume.cpp
    src/core/Volume.h
    src/core/WindowLevel.cpp
    src/core/WindowLevel.h
    src/io/ArchiveExtractor.cpp
    src/io/ArchiveExtractor.h
    src/io/DicomGeometry.cpp
    src/io/DicomGeometry.h
    src/io/DicomReader.cpp
    src/io/DicomReader.h
    src/io/DicomSeries.cpp
    src/io/DicomSeries.h
    src/io/NiftiReader.cpp
    src/io/NiftiReader.h
    src/io/NiftiWriter.cpp
    src/io/NiftiWriter.h
)
target_include_directories(radmarky_core PUBLIC src)
target_link_libraries(radmarky_core PUBLIC
    ${ITK_LIBRARIES}
    LibArchive::LibArchive
    gdcmMSFF
)

add_library(radmarky_validation STATIC
    src/validation/AnnotationValidationService.cpp
    src/validation/AnnotationValidationService.h
    src/validation/PythonValidationEngine.cpp
    src/validation/PythonValidationEngine.h
)
target_include_directories(radmarky_validation PUBLIC src)
target_link_libraries(radmarky_validation PUBLIC
    radmarky_core
    Qt6::Core
)
target_compile_definitions(radmarky_validation PRIVATE
    RADMARKY_DEFAULT_PYTHON_EXECUTABLE="${RADMARKY_DEFAULT_PYTHON_EXECUTABLE}"
)

add_library(radmarky_rendering_core STATIC
    src/rendering/ItkVtkImageBridge.cpp
    src/rendering/ItkVtkImageBridge.h
    src/rendering/OrthogonalReslice.cpp
    src/rendering/OrthogonalReslice.h
)
target_include_directories(radmarky_rendering_core PUBLIC src)
target_link_libraries(radmarky_rendering_core PUBLIC
    radmarky_core
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::ImagingCore
)

set(radmarky_viewer_platform_sources)
if(WIN32)
    set(_radmarky_windows_ico
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/platform/windows/radmarky.ico")
    list(APPEND radmarky_viewer_platform_sources
        resources/platform/windows/radmarky.rc
    )
    set_source_files_properties(resources/platform/windows/radmarky.rc PROPERTIES
        OBJECT_DEPENDS "${_radmarky_windows_ico}"
    )
    unset(_radmarky_windows_ico)
endif()

qt_add_executable(radmarky_viewer WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/app/UserSettings.cpp
    src/app/UserSettings.h
    src/io/AnimationWriter.h
    src/io/AnimatedGifWriter.cpp
    src/io/AnimatedGifWriter.h
    src/io/Mp4Writer.cpp
    src/io/Mp4Writer.h
    src/rendering/OrthogonalViewer.cpp
    src/rendering/OrthogonalViewer.h
    src/rendering/VtkViewport.cpp
    src/rendering/VtkViewport.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/AboutDialog.cpp
    src/ui/AboutDialog.h
    src/ui/DicomSeriesDialog.cpp
    src/ui/DicomSeriesDialog.h
    src/ui/DicomHeaderDialog.cpp
    src/ui/DicomHeaderDialog.h
    src/ui/GifExportDialog.cpp
    src/ui/GifExportDialog.h
    src/ui/CodeEditor.cpp
    src/ui/CodeEditor.h
    src/ui/PythonSyntaxHighlighter.cpp
    src/ui/PythonSyntaxHighlighter.h
    src/ui/UiTheme.cpp
    src/ui/UiTheme.h
    src/ui/ValidationManagementDialog.cpp
    src/ui/ValidationManagementDialog.h
    src/ui/ViewerToolbox.cpp
    src/ui/ViewerToolbox.h
    ${radmarky_viewer_platform_sources}
)

target_link_libraries(radmarky_viewer PRIVATE
    radmarky_app_info
    radmarky_core
    radmarky_rendering_core
    radmarky_validation
    GIF::GIF
    ${FFMPEG_LIBRARIES}
    Qt6::Core
    Qt6::Concurrent
    Qt6::Gui
    Qt6::OpenGLWidgets
    Qt6::Svg
    Qt6::Widgets
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::FiltersSources
    VTK::GUISupportQt
    VTK::ImagingCore
    VTK::InteractionStyle
    VTK::RenderingCore
    VTK::RenderingOpenGL2
)
target_include_directories(radmarky_viewer PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(radmarky_viewer PRIVATE ${FFMPEG_LIBRARY_DIRS})

vtk_module_autoinit(
    TARGETS radmarky_viewer
    MODULES
        VTK::CommonCore
        VTK::CommonDataModel
        VTK::FiltersSources
        VTK::GUISupportQt
        VTK::ImagingCore
        VTK::InteractionStyle
        VTK::RenderingCore
        VTK::RenderingOpenGL2
)

qt_add_resources(radmarky_viewer radmarky_navigation_icons
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES
        resources/icons/app-icon.svg
        resources/icons/brush.svg
        resources/icons/cursor.svg
        resources/icons/document.svg
        resources/icons/invert.svg
        resources/icons/measure.svg
        resources/icons/pan.svg
        resources/icons/contrast.svg
        resources/icons/erase.svg
        resources/icons/eye-hide.svg
        resources/icons/eye-show.svg
        resources/icons/scoped-erase.svg
        resources/icons/circular-target-sniper.svg
        resources/icons/camera.svg
        resources/icons/camcorder.svg
        resources/icons/center-view.svg
        resources/icons/check.svg
        resources/icons/circle.svg
        resources/icons/close-images.svg
        resources/icons/close.svg
        resources/icons/reset-view.svg
        resources/icons/redo.svg
        resources/icons/square.svg
        resources/icons/expand-view.svg
        resources/icons/layout.svg
        resources/icons/new-annotation.svg
        resources/icons/open.svg
        resources/icons/pin-down.svg
        resources/icons/pin-up.svg
        resources/icons/theme.svg
        resources/icons/undo.svg
        resources/icons/validation.svg
        resources/icons/zoom.svg
        resources/icons/zoom-in.svg
        resources/icons/zoom-out.svg
)

qt_add_resources(radmarky_viewer radmarky_validation_presets
    PREFIX "/validation-presets"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/validation-presets"
    FILES
        resources/validation-presets/allowed_labels.py
        resources/validation-presets/continuity.py
        resources/validation-presets/non_empty.py
)

radmarky_setup_qt_runtime(radmarky_viewer)

if(WIN32)
    add_executable(radmarky_update_app_icon tools/update-app-icon.cpp)
    set_target_properties(radmarky_update_app_icon PROPERTIES
        EXCLUDE_FROM_ALL TRUE
        OUTPUT_NAME update-app-icon
    )
    target_link_libraries(radmarky_update_app_icon PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Svg
    )
endif()

if(MSVC)
    target_compile_options(radmarky_app_info PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_core PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_validation PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_rendering_core PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_viewer PRIVATE /W4 /permissive-)
    if(TARGET radmarky_update_app_icon)
        target_compile_options(radmarky_update_app_icon PRIVATE /W4 /permissive-)
    endif()
else()
    target_compile_options(radmarky_app_info PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(radmarky_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(radmarky_validation PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_rendering_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(radmarky_viewer PRIVATE -Wall -Wextra -Wpedantic)
    if(TARGET radmarky_update_app_icon)
        target_compile_options(
            radmarky_update_app_icon PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

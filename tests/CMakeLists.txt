add_executable(radmarky_app_info_tests
    app/ApplicationInfoTest.cpp
)
target_link_libraries(radmarky_app_info_tests PRIVATE radmarky_app_info)

if(MSVC)
    target_compile_options(radmarky_app_info_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(radmarky_app_info_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME application_info COMMAND radmarky_app_info_tests)

add_executable(radmarky_user_settings_tests
    app/UserSettingsTest.cpp
    ../src/app/UserSettings.cpp
    ../src/app/UserSettings.h
)
target_include_directories(radmarky_user_settings_tests PRIVATE ../src)
target_link_libraries(radmarky_user_settings_tests PRIVATE radmarky_core Qt6::Core)

if(MSVC)
    target_compile_options(radmarky_user_settings_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        radmarky_user_settings_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME user_settings COMMAND radmarky_user_settings_tests)

add_executable(radmarky_image_geometry_tests
    core/ImageGeometryTest.cpp
)
target_link_libraries(radmarky_image_geometry_tests PRIVATE radmarky_core)

add_executable(radmarky_annotation_tests
    core/AnnotationTest.cpp
)
target_link_libraries(radmarky_annotation_tests PRIVATE radmarky_core)

add_executable(radmarky_annotation_editor_tests
    core/AnnotationEditorTest.cpp
)
target_link_libraries(radmarky_annotation_editor_tests PRIVATE radmarky_core)

add_executable(radmarky_cursor_sampling_tests
    core/CursorSamplingTest.cpp
)
target_link_libraries(radmarky_cursor_sampling_tests PRIVATE radmarky_core)

add_executable(radmarky_window_level_tests
    core/WindowLevelTest.cpp
)
target_link_libraries(radmarky_window_level_tests PRIVATE radmarky_core)

add_executable(radmarky_nifti_reader_tests
    io/NiftiReaderTest.cpp
)
target_link_libraries(radmarky_nifti_reader_tests PRIVATE radmarky_core)

add_executable(radmarky_nifti_writer_tests
    io/NiftiWriterTest.cpp
)
target_link_libraries(radmarky_nifti_writer_tests PRIVATE radmarky_core)

add_executable(radmarky_dicom_series_tests
    io/DicomSeriesTest.cpp
)
target_link_libraries(radmarky_dicom_series_tests PRIVATE radmarky_core)

add_executable(radmarky_dicom_geometry_tests
    io/DicomGeometryTest.cpp
)
target_link_libraries(radmarky_dicom_geometry_tests PRIVATE radmarky_core)

add_executable(radmarky_dicom_reader_tests
    io/DicomReaderTest.cpp
)
target_link_libraries(radmarky_dicom_reader_tests PRIVATE radmarky_core)

# Manual benchmark: pass a directory containing a representative DICOM study.
# It is intentionally not registered with CTest because timings are hardware- and
# storage-dependent.
add_executable(radmarky_dicom_performance
    io/DicomPerformance.cpp
)
target_link_libraries(radmarky_dicom_performance PRIVATE radmarky_core)

add_executable(radmarky_about_dialog_tests
    ui/AboutDialogTest.cpp
    ../src/ui/AboutDialog.cpp
    ../src/ui/AboutDialog.h
)
target_include_directories(radmarky_about_dialog_tests PRIVATE ../src)
target_link_libraries(
    radmarky_about_dialog_tests PRIVATE radmarky_app_info Qt6::Widgets)

add_executable(radmarky_shortcuts_dialog_tests
    ui/ShortcutsDialogTest.cpp
    ../src/ui/ShortcutsDialog.cpp
    ../src/ui/ShortcutsDialog.h
)
target_include_directories(radmarky_shortcuts_dialog_tests PRIVATE ../src)
target_link_libraries(radmarky_shortcuts_dialog_tests PRIVATE Qt6::Widgets)

add_executable(radmarky_dicom_header_dialog_tests
    ui/DicomHeaderDialogTest.cpp
    ../src/ui/DicomHeaderDialog.cpp
    ../src/ui/DicomHeaderDialog.h
)
target_include_directories(radmarky_dicom_header_dialog_tests PRIVATE ../src)
target_link_libraries(
    radmarky_dicom_header_dialog_tests PRIVATE radmarky_core Qt6::Widgets)

add_executable(radmarky_dicom_series_dialog_tests
    ui/DicomSeriesDialogTest.cpp
    ../src/ui/DicomSeriesDialog.cpp
    ../src/ui/DicomSeriesDialog.h
)
target_include_directories(radmarky_dicom_series_dialog_tests PRIVATE ../src)
target_link_libraries(
    radmarky_dicom_series_dialog_tests PRIVATE radmarky_core Qt6::Widgets)

add_executable(radmarky_gif_export_tests
    ui/GifExportTest.cpp
    ../src/io/AnimationWriter.h
    ../src/io/AnimatedGifWriter.cpp
    ../src/io/AnimatedGifWriter.h
    ../src/io/Mp4Writer.cpp
    ../src/io/Mp4Writer.h
    ../src/ui/GifExportDialog.cpp
    ../src/ui/GifExportDialog.h
)
target_include_directories(radmarky_gif_export_tests PRIVATE ../src)
target_link_libraries(
    radmarky_gif_export_tests PRIVATE GIF::GIF ${FFMPEG_LIBRARIES} Qt6::Widgets)
target_include_directories(
    radmarky_gif_export_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(
    radmarky_gif_export_tests PRIVATE ${FFMPEG_LIBRARY_DIRS})

add_executable(radmarky_validation_management_dialog_tests
    ui/ValidationManagementDialogTest.cpp
    ../src/ui/CodeEditor.cpp
    ../src/ui/CodeEditor.h
    ../src/ui/PythonSyntaxHighlighter.cpp
    ../src/ui/PythonSyntaxHighlighter.h
    ../src/ui/UiTheme.cpp
    ../src/ui/UiTheme.h
    ../src/ui/ValidationManagementDialog.cpp
    ../src/ui/ValidationManagementDialog.h
)
target_include_directories(
    radmarky_validation_management_dialog_tests PRIVATE ../src)
target_link_libraries(
    radmarky_validation_management_dialog_tests PRIVATE
    radmarky_validation Qt6::Svg Qt6::Widgets)

add_executable(radmarky_python_validation_engine_tests
    validation/PythonValidationEngineTest.cpp
)
target_link_libraries(
    radmarky_python_validation_engine_tests PRIVATE radmarky_validation Qt6::Core)

add_executable(radmarky_annotation_validation_service_tests
    validation/AnnotationValidationServiceTest.cpp
)
target_link_libraries(
    radmarky_annotation_validation_service_tests PRIVATE
    radmarky_validation Qt6::Core)

radmarky_setup_qt_runtime(radmarky_about_dialog_tests)
radmarky_setup_qt_runtime(radmarky_shortcuts_dialog_tests)
radmarky_setup_qt_runtime(radmarky_dicom_header_dialog_tests)
radmarky_setup_qt_runtime(radmarky_dicom_series_dialog_tests)
radmarky_setup_qt_runtime(radmarky_gif_export_tests)
radmarky_setup_qt_runtime(radmarky_validation_management_dialog_tests)

add_executable(radmarky_archive_extractor_tests
    io/ArchiveExtractorTest.cpp
)
target_link_libraries(radmarky_archive_extractor_tests PRIVATE radmarky_core)

add_executable(radmarky_orthogonal_reslice_tests
    rendering/OrthogonalResliceTest.cpp
)
target_link_libraries(
    radmarky_orthogonal_reslice_tests PRIVATE radmarky_rendering_core)

if(MSVC)
    target_compile_options(radmarky_image_geometry_tests PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_annotation_tests PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_annotation_editor_tests PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_cursor_sampling_tests PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_window_level_tests PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_nifti_reader_tests PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_nifti_writer_tests PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_dicom_series_tests PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_dicom_geometry_tests PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_dicom_reader_tests PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_dicom_performance PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_about_dialog_tests PRIVATE /W4 /permissive-)
    target_compile_options(
        radmarky_shortcuts_dialog_tests PRIVATE /W4 /permissive-)
    target_compile_options(
        radmarky_dicom_header_dialog_tests PRIVATE /W4 /permissive-)
    target_compile_options(
        radmarky_dicom_series_dialog_tests PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_gif_export_tests PRIVATE /W4 /permissive-)
    target_compile_options(
        radmarky_validation_management_dialog_tests PRIVATE /W4 /permissive-)
    target_compile_options(
        radmarky_python_validation_engine_tests PRIVATE /W4 /permissive-)
    target_compile_options(
        radmarky_annotation_validation_service_tests PRIVATE /W4 /permissive-)
    target_compile_options(radmarky_archive_extractor_tests PRIVATE /W4 /permissive-)
    target_compile_options(
        radmarky_orthogonal_reslice_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        radmarky_image_geometry_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_annotation_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_annotation_editor_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_cursor_sampling_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_window_level_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_nifti_reader_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_nifti_writer_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_dicom_series_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_dicom_geometry_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_dicom_reader_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_dicom_performance PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_about_dialog_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_shortcuts_dialog_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_dicom_header_dialog_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_dicom_series_dialog_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_gif_export_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_validation_management_dialog_tests PRIVATE
        -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_python_validation_engine_tests PRIVATE
        -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_annotation_validation_service_tests PRIVATE
        -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_archive_extractor_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(
        radmarky_orthogonal_reslice_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

vtk_module_autoinit(
    TARGETS radmarky_orthogonal_reslice_tests
    MODULES
        VTK::CommonCore
        VTK::CommonDataModel
        VTK::ImagingCore
)

add_test(NAME image_geometry COMMAND radmarky_image_geometry_tests)
add_test(NAME annotation COMMAND radmarky_annotation_tests)
add_test(NAME annotation_editor COMMAND radmarky_annotation_editor_tests)
add_test(NAME cursor_sampling COMMAND radmarky_cursor_sampling_tests)
add_test(NAME window_level COMMAND radmarky_window_level_tests)
add_test(NAME nifti_reader COMMAND radmarky_nifti_reader_tests)
add_test(NAME nifti_writer COMMAND radmarky_nifti_writer_tests)
add_test(NAME dicom_series COMMAND radmarky_dicom_series_tests)
add_test(NAME dicom_geometry COMMAND radmarky_dicom_geometry_tests)
add_test(NAME dicom_reader COMMAND radmarky_dicom_reader_tests)
add_test(NAME about_dialog COMMAND radmarky_about_dialog_tests)
add_test(NAME shortcuts_dialog COMMAND radmarky_shortcuts_dialog_tests)
add_test(NAME dicom_header_dialog COMMAND radmarky_dicom_header_dialog_tests)
add_test(NAME dicom_series_dialog COMMAND radmarky_dicom_series_dialog_tests)
add_test(NAME gif_export COMMAND radmarky_gif_export_tests)
add_test(
    NAME validation_management_dialog
    COMMAND radmarky_validation_management_dialog_tests)
add_test(
    NAME python_validation_engine
    COMMAND radmarky_python_validation_engine_tests)
add_test(
    NAME annotation_validation_service
    COMMAND radmarky_annotation_validation_service_tests)
add_test(NAME archive_extractor COMMAND radmarky_archive_extractor_tests)
add_test(NAME orthogonal_reslice COMMAND radmarky_orthogonal_reslice_tests)

# Shared vs static Qt. Static builds must import plugins at link time; shared
# Windows builds still need the platforms/ plugin copied beside the executable.

function(radmarky_setup_qt_runtime target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "radmarky_setup_qt_runtime: target '${target}' does not exist")
    endif()
    if(NOT TARGET Qt6::Core)
        return()
    endif()

    get_target_property(_radmarky_qt_type Qt6::Core TYPE)
    if(_radmarky_qt_type STREQUAL "STATIC_LIBRARY")
        set(_radmarky_qt_plugins)
        foreach(_radmarky_qt_plugin IN ITEMS
            Qt6::QWindowsIntegrationPlugin
            Qt6::QWindowsVistaStylePlugin
            Qt6::QModernWindowsStylePlugin
            Qt6::QSvgPlugin
            Qt6::QSvgIconPlugin
        )
            if(TARGET "${_radmarky_qt_plugin}")
                list(APPEND _radmarky_qt_plugins "${_radmarky_qt_plugin}")
            endif()
        endforeach()
        if(_radmarky_qt_plugins)
            qt_import_plugins(${target} INCLUDE ${_radmarky_qt_plugins})
        endif()
        unset(_radmarky_qt_plugins)
        unset(_radmarky_qt_plugin)
        unset(_radmarky_qt_type)
        return()
    endif()
    unset(_radmarky_qt_type)

    if(WIN32 AND TARGET Qt6::QWindowsIntegrationPlugin)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${target}>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
                "$<TARGET_FILE_DIR:${target}>/platforms/"
            COMMENT "Deploying the Qt Windows platform plugin for ${target}"
        )
    endif()
endfunction()

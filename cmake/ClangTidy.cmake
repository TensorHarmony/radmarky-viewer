# Optional clang-tidy entry point. The Visual Studio generator does not export
# compile_commands.json, so the supported Windows workflow is the helper script
# rather than CMAKE_CXX_CLANG_TIDY during every compile.

set(_radmarky_clang_tidy_script
    "${CMAKE_SOURCE_DIR}/tools/run-clang-tidy.ps1")

if(WIN32 AND EXISTS "${_radmarky_clang_tidy_script}")
    find_program(RADMARKY_POWERSHELL NAMES pwsh powershell)
    if(RADMARKY_POWERSHELL)
        add_custom_target(clang_tidy
            COMMAND
                "${RADMARKY_POWERSHELL}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${_radmarky_clang_tidy_script}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Running clang-tidy on src/"
            VERBATIM
        )
        set_target_properties(clang_tidy PROPERTIES
            EXCLUDE_FROM_ALL TRUE
            EXCLUDE_FROM_DEFAULT_BUILD TRUE
        )
    endif()
endif()

unset(_radmarky_clang_tidy_script)

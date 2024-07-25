cmake_minimum_required(VERSION 3.25)

function(CopySharedLibs exe_target)
    if (MSVC)
        set(SlangDLL ${SLANG_DIR}/bin/slang.dll)
        set(DestDir  ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>)
    
        if (NOT TARGET copy_shared_libs)
            add_custom_target(
                copy_shared_libs
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SlangDLL} ${DestDir}
                COMMENT "Copying ${SlangDLL} ${DestDir}"
            )

            set_target_properties(copy_shared_libs PROPERTIES FOLDER "build_tasks")
        endif()

        add_dependencies(${exe_target} copy_shared_libs)
    endif()
endfunction()


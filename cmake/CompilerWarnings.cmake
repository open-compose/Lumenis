# lumenis_set_warnings(<target>) applies the project warning policy.
function(lumenis_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
        if(LUMENIS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow
            -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual
            -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion)
        if(LUMENIS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

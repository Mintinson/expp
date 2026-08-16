include_guard(GLOBAL)

set(EXPP_DEFAULT_VERSION "0.0.1")

function(expp_resolve_version output_variable)
    set(version "${EXPP_DEFAULT_VERSION}")

    if(DEFINED CI_VERSION)
        string(REGEX REPLACE "^v" "" raw_version "${CI_VERSION}")
        string(REGEX MATCH "[0-9]+[.][0-9]+[.][0-9]+" semantic_version "${raw_version}")
        if(semantic_version)
            set(version "${semantic_version}")
        endif()
    endif()

    set(${output_variable} "${version}" PARENT_SCOPE)
endfunction()

# Keep ABI selection independent from the host processor. CMake runs on the
# build machine, but ANDROID_ABI identifies the NDK target being compiled.
function(angelscript_android_call_sources abi output)
        if(abi STREQUAL "arm64-v8a")
                set(sources ../../source/as_callfunc_arm64.cpp ../../source/as_callfunc_arm64_gcc.S)
        elseif(abi STREQUAL "armeabi-v7a")
                set(sources ../../source/as_callfunc_arm.cpp ../../source/as_callfunc_arm_gcc.S)
        elseif(abi STREQUAL "x86" OR abi STREQUAL "x86_64")
                set(sources)
        else()
                message(FATAL_ERROR "Unsupported Android ABI for AngelScript: '${abi}'")
        endif()
        set(${output} "${sources}" PARENT_SCOPE)
endfunction()

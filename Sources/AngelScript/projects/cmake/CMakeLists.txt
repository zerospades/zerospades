cmake_minimum_required(VERSION 3.10)
set(CMAKE_ALLOW_LOOSE_LOOP_CONSTRUCTS TRUE)
cmake_policy(SET CMP0003 NEW)

project(Angelscript)

set(ANGELSCRIPT_SOURCE
        ../../source/as_atomic.cpp
        ../../source/as_builder.cpp
        ../../source/as_bytecode.cpp
        ../../source/as_callfunc.cpp
        ../../source/as_callfunc_x86.cpp
        ../../source/as_callfunc_x64_gcc.cpp
        ../../source/as_callfunc_x64_msvc.cpp
        ../../source/as_compiler.cpp
        ../../source/as_configgroup.cpp
        ../../source/as_context.cpp
        ../../source/as_datatype.cpp
        ../../source/as_gc.cpp
        ../../source/as_generic.cpp
        ../../source/as_globalproperty.cpp
        ../../source/as_memory.cpp
        ../../source/as_module.cpp
        ../../source/as_objecttype.cpp
        ../../source/as_outputbuffer.cpp
        ../../source/as_parser.cpp
        ../../source/as_restore.cpp
        ../../source/as_scriptcode.cpp
        ../../source/as_scriptengine.cpp
        ../../source/as_scriptfunction.cpp
        ../../source/as_scriptnode.cpp
        ../../source/as_scriptobject.cpp
        ../../source/as_string.cpp
        ../../source/as_string_util.cpp
        ../../source/as_thread.cpp
        ../../source/as_tokenizer.cpp
        ../../source/as_typeinfo.cpp
        ../../source/as_variablescope.cpp
)

# Enable optimized x64/IA64 MASM assembly (ml64.exe)
# MASM (ml64.exe) only supports x64 and IA64. ARM64 requires armasm64.exe (different tool).
# MSVC_CXX_ARCHITECTURE_ID can be "X64", "x64", "IA64", "ia64", "amd64", "ARM64", "arm64" etc. depending on CMake/MSVC version.
# Use CMAKE_CL_64 (reliable for all 64-bit compilers) but explicitly exclude ARM64 (case-insensitive).
if(MSVC AND CMAKE_CL_64 AND NOT MSVC_CXX_ARCHITECTURE_ID MATCHES "^(ARM64|arm64)")
	enable_language(ASM_MASM)
	if(CMAKE_ASM_MASM_COMPILER_WORKS)
		set(ANGELSCRIPT_SOURCE ${ANGELSCRIPT_SOURCE} ../../source/as_callfunc_x64_msvc_asm.asm)
	else()
		message(FATAL_ERROR "MASM x64/IA64 target requires a working assembler")
	endif()
endif()

if(ANDROID)
        enable_language(ASM)
        if(CMAKE_ASM_COMPILER_WORKS)
                set(ANGELSCRIPT_SOURCE ${ANGELSCRIPT_SOURCE} ../../source/as_callfunc_arm.cpp ../../source/as_callfunc_arm_gcc.S)
        else()
                message(FATAL ERROR "Android target requires a working assembler")
        endif(CMAKE_ASM_COMPILER_WORKS)
endif()

# Add ARM64 support for macOS (Apple Silicon M1/M2/M3)
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        enable_language(ASM)
        if(CMAKE_ASM_COMPILER_WORKS)
                set(ANGELSCRIPT_SOURCE ${ANGELSCRIPT_SOURCE} ../../source/as_callfunc_arm64.cpp ../../source/as_callfunc_arm64_xcode.S)
                message(STATUS "Building AngelScript with ARM64 support for macOS")
        else()
                message(FATAL_ERROR "macOS ARM64 target requires a working assembler")
        endif()
endif()

# Add ARM64 support for Linux
if(UNIX AND NOT APPLE AND NOT ANDROID AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        enable_language(ASM)
        if(CMAKE_ASM_COMPILER_WORKS)
                set(ANGELSCRIPT_SOURCE ${ANGELSCRIPT_SOURCE} ../../source/as_callfunc_arm64.cpp ../../source/as_callfunc_arm64_gcc.S)
                message(STATUS "Building AngelScript with ARM64 support for Linux")
        else()
                message(FATAL_ERROR "Linux ARM64 target requires a working assembler")
        endif()
endif()

# Add ARM64 support for Windows (MSVC)
# as_config.h defines AS_ARM64 whenever _M_ARM64 is set, so AngelScript expects the
# native ARM64 implementation to be present. That implementation is
# as_callfunc_arm64.cpp plus as_callfunc_arm64_msvc.asm, which is assembled by
# armasm64.exe (CMake exposes it as the ASM_MARMASM language, available since 3.26).
#
# Do NOT substitute AS_MAX_PORTABILITY here: it compiles as_callfunc_arm64.cpp out
# (the file is guarded by #ifndef AS_MAX_PORTABILITY) and restricts the engine to
# asCALL_GENERIC, which every asFUNCTION()/asMETHOD() registration rejects with
# asWRONG_CALLING_CONV in DetectCallingConvention() (as_callfunc.cpp).
include(${CMAKE_SOURCE_DIR}/cmake/DetectARM64.cmake)
if(IS_ARM64)
	if(CMAKE_VERSION VERSION_LESS 3.26)
		message(FATAL_ERROR
			"Windows ARM64 requires CMake 3.26 or newer for ASM_MARMASM (armasm64) support. "
			"Detected CMake ${CMAKE_VERSION}.")
	endif()

	enable_language(ASM_MARMASM)
	if(NOT CMAKE_ASM_MARMASM_COMPILER_WORKS)
		message(FATAL_ERROR "Windows ARM64 target requires a working armasm64 assembler")
	endif()

	set(ANGELSCRIPT_SOURCE ${ANGELSCRIPT_SOURCE}
		../../source/as_callfunc_arm64.cpp
		../../source/as_callfunc_arm64_msvc.asm)
	message(STATUS "[AngelScript] Building with native ARM64 support for Windows")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
    enable_language(ASM)
    if(CMAKE_ASM_COMPILER_WORKS)
        set(ANGELSCRIPT_SOURCE ${ANGELSCRIPT_SOURCE} ../../source/as_callfunc_riscv64.cpp ../../source/as_callfunc_riscv64_gcc.S)
    else()
        message(FATAL ERROR "RISC-V 64 target requires a working assembler")
    endif()
endif()

set(ANGELSCRIPT_HEADERS
        ../../include/angelscript.h
        ../../source/as_array.h
        ../../source/as_builder.h
        ../../source/as_bytecode.h
        ../../source/as_callfunc.h
        ../../source/as_compiler.h
        ../../source/as_config.h
        ../../source/as_configgroup.h
        ../../source/as_context.h
        ../../source/as_criticalsection.h
        ../../source/as_datatype.h
        ../../source/as_debug.h
        ../../source/as_generic.h
        ../../source/as_map.h
        ../../source/as_memory.h
        ../../source/as_module.h
        ../../source/as_namespace.h
        ../../source/as_objecttype.h
        ../../source/as_outputbuffer.h
        ../../source/as_parser.h
        ../../source/as_property.h
        ../../source/as_restore.h
        ../../source/as_scriptcode.h
        ../../source/as_scriptengine.h
        ../../source/as_scriptfunction.h
        ../../source/as_scriptnode.h
        ../../source/as_scriptobject.h
        ../../source/as_string.h
        ../../source/as_string_util.h
        ../../source/as_texts.h
        ../../source/as_thread.h
        ../../source/as_tokendef.h
        ../../source/as_tokenizer.h
        ../../source/as_typeinfo.h
        ../../source/as_variablescope.h
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)

add_definitions("-D_CRT_SECURE_NO_WARNINGS -DANGELSCRIPT_EXPORT -D_LIB")

# Fix x64 issues on Linux
if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64" AND NOT APPLE)
        add_definitions(-fPIC)
endif()

add_library(Angelscript STATIC ${ANGELSCRIPT_SOURCE} ${ANGELSCRIPT_HEADERS})
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)

find_package(Threads)
target_link_libraries(Angelscript ${CMAKE_THREAD_LIBS_INIT})

# /MP is a cl.exe switch. A target-wide COMPILE_FLAGS is emitted into every compile
# item, including the assembler ones, and armasm64.exe aborts on an unrecognised
# argument (error A2029). Scope it to the C/C++ sources so the assembly stays clean.
if(MSVC)
        set(ANGELSCRIPT_CL_SOURCE ${ANGELSCRIPT_SOURCE})
        list(FILTER ANGELSCRIPT_CL_SOURCE EXCLUDE REGEX "\\.(asm|S)$")
        set_source_files_properties(${ANGELSCRIPT_CL_SOURCE} PROPERTIES COMPILE_FLAGS "/MP")
endif(MSVC)

set(RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/../../bin)


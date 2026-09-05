# Detect a Windows ARM64 (MSVC) build and expose the result as IS_ARM64.
# This module provides consistent ARM64 detection across all CMake build files;
# include it rather than re-implementing the checks.
#
# Usage:
#   include(${CMAKE_SOURCE_DIR}/cmake/DetectARM64.cmake)
#   if(IS_ARM64)
#       # ARM64-specific configuration
#   endif()
#
# IS_ARM64 is always defined (FALSE on non-MSVC toolchains).

if(MSVC)
	if(NOT DEFINED IS_ARM64)
		set(IS_ARM64 FALSE)
		# Convert architecture strings once to avoid redundant conversions
		string(TOUPPER "${MSVC_CXX_ARCHITECTURE_ID}" _arm64_arch_upper)
		string(TOUPPER "${CMAKE_GENERATOR_PLATFORM}" _arm64_gen_upper)

		# Method 1: CMAKE_VS_PLATFORM_NAME (Visual Studio generator, most reliable)
		string(TOUPPER "${CMAKE_VS_PLATFORM_NAME}" _arm64_platform_upper)
		if(_arm64_platform_upper STREQUAL "ARM64")
			set(IS_ARM64 TRUE)
			message(STATUS "ARM64 detected via CMAKE_VS_PLATFORM_NAME")
		endif()

		# Method 2: MSVC_CXX_ARCHITECTURE_ID (MSVC toolchain detection)
		if(NOT IS_ARM64)
			if(_arm64_arch_upper STREQUAL "ARM64")
				set(IS_ARM64 TRUE)
				message(STATUS "ARM64 detected via MSVC_CXX_ARCHITECTURE_ID")
			endif()
		endif()

		# Method 3: CMAKE_SYSTEM_PROCESSOR (system-level detection)
		if(NOT IS_ARM64)
			string(TOUPPER "${CMAKE_SYSTEM_PROCESSOR}" _arm64_proc_upper)
			if(_arm64_proc_upper MATCHES "ARM64|AARCH64")
				set(IS_ARM64 TRUE)
				message(STATUS "ARM64 detected via CMAKE_SYSTEM_PROCESSOR")
			endif()
		endif()

		# Method 4: Explicit non-x86 detection - only set ARM64 if we're not explicitly building for x86/x64
		if(NOT IS_ARM64)
			if(NOT _arm64_arch_upper MATCHES "X86|X64|IA64" AND
			   NOT _arm64_gen_upper MATCHES "WIN32|X64")
				set(IS_ARM64 TRUE)
				message(STATUS "ARM64 assumed (not x86/x64)")
			endif()
		endif()

		if(IS_ARM64)
			message(STATUS "Windows ARM64 build detected")
		else()
			message(STATUS "Not ARM64 (CMAKE_VS_PLATFORM_NAME=${CMAKE_VS_PLATFORM_NAME}, MSVC_CXX_ARCHITECTURE_ID=${MSVC_CXX_ARCHITECTURE_ID})")
		endif()
	endif()
else()
	set(IS_ARM64 FALSE)
endif()

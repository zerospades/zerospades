cmake_minimum_required(VERSION 3.10)
project(ZeroSpades)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# OpenSpades client identity sent to AoS servers in the version handshake.
# Do NOT change — frozen at 0.1.5 so servers continue to recognise us.
set(OPENSPADES_VERSION_MAJOR 0)
set(OPENSPADES_VERSION_MINOR 1)
set(OPENSPADES_VERSION_PATCH 5)

# ZeroSpades application version (the actual fork build number).
set(ZEROSPADES_VERSION_MAJOR 0)
set(ZEROSPADES_VERSION_MINOR 0)
set(ZEROSPADES_VERSION_PATCH 9)
set(ZEROSPADES_VERSION_FULL "${ZEROSPADES_VERSION_MAJOR}.${ZEROSPADES_VERSION_MINOR}.${ZEROSPADES_VERSION_PATCH}")

set(OS_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Prefer GLVND OpenGL
if(POLICY CMP0072)
	cmake_policy(SET CMP0072 NEW)
endif()

if(ANDROID)
	include(android/native/AndroidDependencies.cmake)
else()
include(cmake/FindSDL2.cmake)
if(NOT SDL2_FOUND)
	message(FATAL_ERROR "SDL 2.0 not found, set ENV{SDL2DIR} to point to SDL 2.0, and optionally set the cmake var SDL2_LIBRARY_TEMP to the lib dir")
endif()

include(cmake/FindSDL2_image.cmake)
if(NOT SDL2_IMAGE_FOUND)
	message(FATAL_ERROR "SDL_image 2.0 not found, set ENV{SDL2DIR} to point to SDL 2.0, and optionally set the cmake var SDL2_LIBRARY_TEMP to the lib dir")
endif()

include(FindOpenGL)
if(NOT OPENGL_FOUND AND NOT OPENGL_XMESA_FOUND AND NOT OPENGL_GLU_FOUND)
	message(FATAL_ERROR "OpenGL not found, please install it")
endif()
if(NOT APPLE)
	include(cmake/FindGLEW2.cmake)
	if(NOT GLEW_FOUND)
		message(FATAL_ERROR "GLEW not found, please install it and make sure CMake can find it (add it to the PATH)")
	endif()
endif()

include(FindZLIB)
if(NOT ZLIB_FOUND)
	message(FATAL_ERROR "ZLIB not found, manually set ZLIB_ROOT in CMake")
endif()

include(FindCURL)
if(NOT CURL_FOUND)
	message(FATAL_ERROR "cURL not found, please install it (and make sure it's in your path)")
endif()

include(FindFreetype)
if(NOT FREETYPE_FOUND)
	message(FATAL_ERROR "FreeType not found, please install it (and make sure it's in your path)")
endif()

include(cmake/FindOgg.cmake)
if(NOT Ogg_FOUND)
	message(FATAL_ERROR "libogg not found, please install it (and make sure it's in your path)")
endif()

include(cmake/FindOpus.cmake)
if(NOT OpusFile_FOUND)
	message(FATAL_ERROR "libopus/libopusfile not found, please install it (and make sure it's in your path)")
endif()
endif() # ANDROID dependency discovery

# OpenAL configuration
option(USE_INTERNAL_OPENAL "Use bundled OpenAL Soft instead of system OpenAL" ON)

if(USE_INTERNAL_OPENAL)
	if(ANDROID)
		# Keep the SDL game library self-contained apart from the NDK runtime.
		set(LIBTYPE "STATIC" CACHE STRING "OpenAL library type" FORCE)
		# Fail the build rather than shipping a native client without audio.
		set(ALSOFT_REQUIRE_OPENSL ON CACHE BOOL "Require Android OpenSL backend" FORCE)
	endif()
	# Add OpenAL Soft as a subdirectory
	option(ALSOFT_UTILS "Build utility programs" OFF)
	option(ALSOFT_NO_CONFIG_UTIL "Disable building the alsoft-config utility" ON)
	option(ALSOFT_EXAMPLES "Build example programs" OFF)
	option(ALSOFT_INSTALL "Install headers and libraries" OFF)
	option(ALSOFT_INSTALL_CONFIG "Install alsoft.conf sample configuration file" OFF)
	option(ALSOFT_INSTALL_HRTF_DATA "Install HRTF data files" OFF)
	option(ALSOFT_INSTALL_AMBDEC_PRESETS "Install AmbiDec preset files" OFF)
	option(ALSOFT_INSTALL_EXAMPLES "Install example programs (alplay, alstream, ...)" OFF)
	option(ALSOFT_INSTALL_UTILS "Install utility programs (openal-info, alsoft-config, ...)" OFF)
	add_subdirectory(openal-soft)
	if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		foreach(OPENAL_TARGET OpenAL alsoft.common alsoft.fmt)
			if(TARGET ${OPENAL_TARGET})
				target_compile_options(${OPENAL_TARGET} PRIVATE -w)
			endif()
		endforeach()
	endif()

	# Use OpenAL Soft instead of system OpenAL
	set(OPENAL_FOUND TRUE)
	set(OPENAL_LIBRARY OpenAL)
	set(OPENAL_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/openal-soft/include)
else()
	# Use system OpenAL
	find_package(OpenAL REQUIRED)
endif()

if(CMAKE_COMPILER_IS_GNUCXX)
	message(STATUS "C++17 activated.")

	if(CMAKE_BUILD_TYPE MATCHES "Release")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
		set(CMAKE_EXE_LINKER_FLAGS "-s")
	endif()

elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	if(CMAKE_BUILD_TYPE MATCHES "Release")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
		set(CMAKE_EXE_LINKER_FLAGS "-s")
	endif()

elseif(MSVC)
	add_definitions(
		/D_CRT_SECURE_NO_WARNINGS
		/wd4068
		/DNOMINMAX
		/D_USE_MATH_DEFINES
	)
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /O2 /GL")	#static link to msv runtime (MSVCR(P)xxx.dll)
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
	#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)	#msvc / xcode: group files in project


#mainly for ENet...
include(CheckFunctionExists)
include(CheckStructHasMember)
include(CheckTypeSize)
check_function_exists("fcntl" HAS_FCNTL)
check_function_exists("poll" HAS_POLL)
check_function_exists("gethostbyname_r" HAS_GETHOSTBYNAME_R)
check_function_exists("gethostbyaddr_r" HAS_GETHOSTBYADDR_R)
check_function_exists("inet_pton" HAS_INET_PTON)
check_function_exists("inet_ntop" HAS_INET_NTOP)
check_struct_has_member("struct msghdr" "msg_flags" "sys/types.h;sys/socket.h" HAS_MSGHDR_FLAGS)
set(CMAKE_EXTRA_INCLUDE_FILES "sys/types.h" "sys/socket.h")
check_type_size("socklen_t" HAS_SOCKLEN_T BUILTIN_TYPES_ONLY)
unset(CMAKE_EXTRA_INCLUDE_FILES)

if(HAS_FCNTL)
	add_definitions(-DHAS_FCNTL=1)
endif()
if(HAS_POLL)
	add_definitions(-DHAS_POLL=1)
endif()
if(HAS_GETHOSTBYNAME_R)
	add_definitions(-DHAS_GETHOSTBYNAME_R=1)
endif()
if(HAS_GETHOSTBYADDR_R)
	add_definitions(-DHAS_GETHOSTBYADDR_R=1)
endif()
if(HAS_INET_PTON)
	add_definitions(-DHAS_INET_PTON=1)
endif()
if(HAS_INET_NTOP)
	add_definitions(-DHAS_INET_NTOP=1)
endif()
if(HAS_MSGHDR_FLAGS)
	add_definitions(-DHAS_MSGHDR_FLAGS=1)
endif()
if(HAS_SOCKLEN_T)
	add_definitions(-DHAS_SOCKLEN_T=1)
endif()

option(ZEROSPADES_RESOURCES "Build game assets" ON)
option(ZEROSPADES_NONFREE_RESOURCES "Download non-GPL game assets" ON)

set(ZEROSPADES_RESDIR "" CACHE STRING "Resource dir to use while developing")
set(ZEROSPADES_EXTRATITLE "" CACHE STRING "Extra info to add to the package name")
if(ZEROSPADES_RESDIR)
	set(RESDIR ${ZEROSPADES_RESDIR})
	set(RESDIR_DEFINED TRUE)
	mark_as_advanced(RESDIR)
	mark_as_advanced(RESDIR_DEFINED)
endif()


if(UNIX)
	EXECUTE_PROCESS(COMMAND "date" "+%Y" OUTPUT_VARIABLE CMAKE_CURRENT_YEAR)
	mark_as_advanced(CMAKE_CURRENT_YEAR)
endif(UNIX)

if(CMAKE_CXX_COMPILER_ID)
	if(CMAKE_CXX_COMPILER_VERSION)
		set(_version " ${CMAKE_CXX_COMPILER_VERSION}")
	else()
		set(_version "")
	endif()
	set(ZEROSPADES_COMPILER ${CMAKE_CXX_COMPILER_ID}${_version})
endif()


configure_file("${PROJECT_SOURCE_DIR}/ZeroSpades.h.in" "${PROJECT_BINARY_DIR}/ZeroSpades.h")
include_directories(BEFORE "${PROJECT_BINARY_DIR}")
include_directories("${SDL2_INCLUDE_DIR}")
include_directories("${SDL2_IMAGE_INCLUDE_DIR}")
if(OPENGL_INCLUDE_DIR)
	include_directories("${OPENGL_INCLUDE_DIR}")
endif()
if(NOT APPLE AND NOT ANDROID)
include_directories("${GLEW_INCLUDE_DIR}")
endif()
include_directories("${ZLIB_INCLUDE_DIR}")
include_directories(${CURL_INCLUDE_DIRS})
include_directories(${FREETYPE_INCLUDE_DIRS})
include_directories(${Ogg_INCLUDE_DIR})
include_directories(${OpusFile_INCLUDE_DIR})
include_directories(${Opus_INCLUDE_DIR})
include_directories(${Ogg_INCLUDE_DIR})

add_subdirectory(Resources)
add_subdirectory(Sources)

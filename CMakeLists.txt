cmake_minimum_required(VERSION 3.10)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
	set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment version")
endif()

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

if(VCPKG_TARGET_TRIPLET)
	set(USE_VCPKG ON)
else()
	set(USE_VCPKG OFF)
endif()

# Prefer GLVND OpenGL
if(POLICY CMP0072)
	cmake_policy(SET CMP0072 NEW)
endif()

# Rendering backend options
option(USE_OPENGL "Enable OpenGL renderer" ON)
option(USE_VULKAN "Enable Vulkan renderer" ON)
# macOS Debug builds only: link the Vulkan loader instead of MoltenVK so
# validation layers can be injected. Produces a bundle that is not
# self-contained, so it is rejected outside Debug builds.
option(ZS_VULKAN_USE_LOADER "macOS Debug only: link the Vulkan loader instead of MoltenVK (enables validation layers)" OFF)

if(USE_VCPKG)
	find_package(Ogg CONFIG REQUIRED)
	find_package(Opus CONFIG REQUIRED)
	find_package(SDL2_image CONFIG REQUIRED)
	find_package(SDL2 CONFIG REQUIRED)
	find_package(freetype CONFIG REQUIRED)

	if(NOT APPLE)
		find_package(GLEW REQUIRED)
		set(GLEW_LIBRARY GLEW::GLEW)

		find_package(CURL REQUIRED)
		set(CURL_LIBRARY CURL::libcurl)
	endif()

	set(SDL2_LIBRARY SDL2::SDL2main SDL2::SDL2-static)
	set(SDL2_IMAGE_LIBRARY SDL2_image::SDL2_image-static)
	set(FREETYPE_LIBRARIES freetype)

	if(APPLE)
		# Use system libraries
		include(FindOpenGL)
		include(FindCURL)
	endif()

	FIND_LIBRARY(OpusFile_LIBRARY
	  NAMES opusfile
	  PATH_SUFFIXES lib64 lib
	  PATHS ${CMAKE_PREFIX_PATH}
	)
else()
	include(cmake/FindSDL2.cmake)
	if(NOT SDL2_FOUND)
		message(FATAL_ERROR "SDL 2.0 not found, set ENV{SDL2DIR} to point to SDL 2.0, and optionally set the cmake var SDL2_LIBRARY_TEMP to the lib dir")
	endif()

	include(cmake/FindSDL2_image.cmake)
	if(NOT SDL2_IMAGE_FOUND)
		message(FATAL_ERROR "SDL_image 2.0 not found, set ENV{SDL2DIR} to point to SDL 2.0, and optionally set the cmake var SDL2_LIBRARY_TEMP to the lib dir")
	endif()

	if(USE_OPENGL)
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
endif()

# Vulkan configuration
if(USE_VULKAN)
	if(USE_VCPKG)
		# All Vulkan deps come from vcpkg — no system installation required.
		find_package(VulkanHeaders CONFIG REQUIRED)
		get_target_property(Vulkan_INCLUDE_DIRS Vulkan::Headers INTERFACE_INCLUDE_DIRECTORIES)
		message(STATUS "Vulkan headers (vcpkg): ${Vulkan_INCLUDE_DIRS}")

		if(APPLE)
			find_package(molten-vk CONFIG REQUIRED)
			set(MOLTENVK_LIBRARY MoltenVK::MoltenVK)
			message(STATUS "Using MoltenVK (vcpkg): ${MOLTENVK_LIBRARY}")
		else()
			find_package(Vulkan REQUIRED)
			message(STATUS "Vulkan loader (vcpkg): ${Vulkan_LIBRARIES}")
		endif()

		find_program(GLSLANG_VALIDATOR glslangValidator
			HINTS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/glslang"
			NO_DEFAULT_PATH)
	else()
		if(APPLE AND ZS_VULKAN_USE_LOADER)
			# Debugging configuration: go through the Vulkan loader rather than
			# MoltenVK. Validation layers are injected BY the loader, so they can
			# never load when MoltenVK is linked directly, no matter which
			# environment variables are set. The loader dispatches to MoltenVK
			# through its ICD manifest (/opt/local/share/vulkan/icd.d).
			#
			# Restricted to Debug builds so a release bundle can never end up
			# depending on a loader and ICD installed on the developer's machine
			# instead of being self-contained.
			if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
				message(FATAL_ERROR
					"ZS_VULKAN_USE_LOADER is a debugging aid and requires -DCMAKE_BUILD_TYPE=Debug "
					"(current: '${CMAKE_BUILD_TYPE}'). It links the Vulkan loader instead of "
					"MoltenVK, which makes the bundle non-portable.")
			endif()
			find_library(VULKAN_LOADER_LIBRARY NAMES vulkan vulkan.1
				HINTS /opt/local/lib /opt/homebrew/lib /usr/local/lib
				      ENV VULKAN_SDK
				PATH_SUFFIXES lib)
			if(NOT VULKAN_LOADER_LIBRARY)
				message(FATAL_ERROR "Vulkan loader not found. Install via: sudo port install vulkan-loader")
			endif()
			set(MOLTENVK_LIBRARY "${VULKAN_LOADER_LIBRARY}")
			message(STATUS "Using Vulkan loader (validation-capable): ${MOLTENVK_LIBRARY}")

			find_path(Vulkan_INCLUDE_DIRS NAMES vulkan/vulkan.h
				HINTS /opt/local/include /opt/homebrew/include /usr/local/include
				      ENV VULKAN_SDK
				PATH_SUFFIXES include)
			if(NOT Vulkan_INCLUDE_DIRS)
				message(FATAL_ERROR "Vulkan headers not found. Install via: port install vulkan-headers")
			endif()
			message(STATUS "Vulkan headers: ${Vulkan_INCLUDE_DIRS}")
		elseif(APPLE)
			# On macOS, link directly to MoltenVK instead of the Vulkan loader.
			# MoltenVK exports all Vulkan entry points, so the loader is unnecessary
			# and this makes the .app bundle self-contained.
			find_library(MOLTENVK_LIBRARY NAMES MoltenVK
				HINTS /opt/local/lib /opt/homebrew/lib /usr/local/lib
				      ENV VULKAN_SDK
				PATH_SUFFIXES lib)
			if(NOT MOLTENVK_LIBRARY)
				message(FATAL_ERROR "MoltenVK not found. Install via: port install MoltenVK-latest  OR  brew install molten-vk")
			endif()
			message(STATUS "Using MoltenVK directly: ${MOLTENVK_LIBRARY}")

			# Find Vulkan headers (provided by vulkan-headers or molten-vk package)
			find_path(Vulkan_INCLUDE_DIRS NAMES vulkan/vulkan.h
				HINTS /opt/local/include /opt/homebrew/include /usr/local/include
				      ENV VULKAN_SDK
				PATH_SUFFIXES include)
			if(NOT Vulkan_INCLUDE_DIRS)
				message(FATAL_ERROR "Vulkan headers not found. Install via: port install vulkan-headers  OR  brew install vulkan-headers")
			endif()
			message(STATUS "Vulkan headers: ${Vulkan_INCLUDE_DIRS}")
		else()
			# On non-Apple platforms, use the standard Vulkan SDK
			find_package(Vulkan REQUIRED)
			message(STATUS "Vulkan SDK found: ${Vulkan_INCLUDE_DIRS}")
			message(STATUS "Vulkan library: ${Vulkan_LIBRARIES}")
		endif()

		find_program(GLSLANG_VALIDATOR
			NAMES glslangValidator
			HINTS /opt/local/bin /opt/homebrew/bin /usr/local/bin
			      ${Vulkan_INCLUDE_DIRS}/../bin
			      ENV VULKAN_SDK
			PATH_SUFFIXES bin)
	endif()

	if(GLSLANG_VALIDATOR)
		message(STATUS "glslangValidator found: ${GLSLANG_VALIDATOR}")
	else()
		message(FATAL_ERROR "glslangValidator not found - required for Vulkan shader compilation. Install the Vulkan SDK or glslang package.")
	endif()

	# Find spirv-cross for shader reflection (optional)
	if(USE_VCPKG)
		find_package(spirv_cross_core CONFIG QUIET)
		if(spirv_cross_core_FOUND)
			set(SPIRV_CROSS_LIBRARIES spirv-cross-core)
			set(HAVE_SPIRV_CROSS TRUE)
			message(STATUS "spirv-cross library found (vcpkg)")
		else()
			set(HAVE_SPIRV_CROSS FALSE)
			message(STATUS "spirv-cross library not found - shader reflection disabled")
		endif()
	else()
		find_library(SPIRV_CROSS_LIBRARY NAMES spirv-cross-core
			HINTS /opt/local/lib /opt/homebrew/lib /usr/local/lib)
		find_path(SPIRV_CROSS_INCLUDE_DIR NAMES spirv_cross/spirv_cross.hpp
			HINTS /opt/local/include /opt/homebrew/include /usr/local/include)

		if(SPIRV_CROSS_LIBRARY AND SPIRV_CROSS_INCLUDE_DIR)
			message(STATUS "spirv-cross library found: ${SPIRV_CROSS_LIBRARY}")
			set(SPIRV_CROSS_LIBRARIES ${SPIRV_CROSS_LIBRARY})
			set(HAVE_SPIRV_CROSS TRUE)
		else()
			message(STATUS "spirv-cross library not found - shader reflection disabled")
			set(HAVE_SPIRV_CROSS FALSE)
		endif()
	endif()
endif()

# OpenAL configuration
option(USE_INTERNAL_OPENAL "Use bundled OpenAL Soft instead of system OpenAL" ON)

if(USE_INTERNAL_OPENAL)
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
		if(NOT APPLE)
			set(CMAKE_EXE_LINKER_FLAGS "-s")
		endif()
	endif()

elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	if(CMAKE_BUILD_TYPE MATCHES "Release")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
		if(NOT APPLE)
			set(CMAKE_EXE_LINKER_FLAGS "-s")
		endif()
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

# Add compile definitions for enabled renderers
if(USE_OPENGL)
	add_definitions(-DUSE_OPENGL=1)
	message(STATUS "OpenGL renderer: ENABLED")
else()
	message(STATUS "OpenGL renderer: DISABLED")
endif()

if(USE_VULKAN)
	add_definitions(-DUSE_VULKAN=1)
	message(STATUS "Vulkan renderer: ENABLED")
else()
	message(STATUS "Vulkan renderer: DISABLED")
endif()

# Ensure at least one renderer is enabled
if(NOT USE_OPENGL AND NOT USE_VULKAN)
	message(FATAL_ERROR "At least one renderer must be enabled (USE_OPENGL or USE_VULKAN)")
endif()

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

# note that all paths are without trailing slash
set(ZEROSPADES_INSTALL_DOC		 "share/doc/zerospades" CACHE STRING "Directory for installing documentation. ")
set(ZEROSPADES_INSTALL_MENU		 "share/menu"			CACHE STRING "Directory for installing menu file. " )
set(ZEROSPADES_INSTALL_MAN		 "share/man/man6"		CACHE STRING "Directory for installing man file. " )
set(ZEROSPADES_INSTALL_RESOURCES "share/games/zerospades/Resources" CACHE STRING "Directory for installing Resources. ")
set(ZEROSPADES_INSTALL_ICONS	 "share/icons/hicolor"	CACHE STRING "Directory for installing icons. ")
set(ZEROSPADES_INSTALL_PIXMAP	 "share/pixmaps"		CACHE STRING "Directory for installing .xpm icon. ")
set(ZEROSPADES_INSTALL_BINARY	 "games"				CACHE STRING "Directory for installing result binary executable. ")
set(ZEROSPADES_INSTALL_DESKTOP	 "share/applications"	CACHE STRING "Directory for installing .desktop file. ")
set(ZEROSPADES_INSTALL_LIBS		 "lib/zerospades"		CACHE STRING "Directory for installing various non-media stuff. Currently not used")

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
configure_file("${PROJECT_SOURCE_DIR}/ZeroSpades.rc.in" "${PROJECT_BINARY_DIR}/ZeroSpades.rc")
include_directories(BEFORE "${PROJECT_BINARY_DIR}")
if(USE_VCPKG)
	include_directories("${PROJECT_SOURCE_DIR}/vcpkg/installed/${VCPKG_TARGET_TRIPLET}/include")
	include_directories("${PROJECT_SOURCE_DIR}/vcpkg/installed/${VCPKG_TARGET_TRIPLET}/include/opus")
else()
	include_directories("${SDL2_INCLUDE_DIR}")
	include_directories("${SDL2_IMAGE_INCLUDE_DIR}")
	if(OPENGL_INCLUDE_DIR)
		include_directories("${OPENGL_INCLUDE_DIR}")
	endif()
	if(NOT APPLE)
	include_directories("${GLEW_INCLUDE_DIR}")
	endif()
	include_directories("${ZLIB_INCLUDE_DIR}")
	include_directories(${CURL_INCLUDE_DIRS})
	include_directories(${FREETYPE_INCLUDE_DIRS})
	include_directories(${Ogg_INCLUDE_DIR})
	include_directories(${OpusFile_INCLUDE_DIR})
	include_directories(${Opus_INCLUDE_DIR})
	include_directories(${Ogg_INCLUDE_DIR})
endif()

add_subdirectory(Resources)
add_subdirectory(Sources)

if(UNIX AND NOT APPLE)
	# various texts
	install(FILES AUTHORS LICENSE README.md DESTINATION ${ZEROSPADES_INSTALL_DOC}/)

	# menu file
	install(FILES Resources/Unix/Menu/zerospades DESTINATION ${ZEROSPADES_INSTALL_MENU}/)

	# man file
	install(FILES ${CMAKE_BINARY_DIR}/Resources/Unix/Man/zerospades.6.gz DESTINATION ${ZEROSPADES_INSTALL_MAN}/)

	# changelog file
	install(FILES ${CMAKE_BINARY_DIR}/Resources/Unix/Debian/changelog.gz DESTINATION ${ZEROSPADES_INSTALL_DOC}/)

	# copyright file
	install(FILES ${CMAKE_BINARY_DIR}/Resources/Unix/Debian/copyright DESTINATION ${ZEROSPADES_INSTALL_DOC}/)

	if(ZEROSPADES_RESOURCES)
		# install asset paks (including non-GPL one)
		install(FILES ${PAK_FILES} DESTINATION ${ZEROSPADES_INSTALL_RESOURCES})
	endif(ZEROSPADES_RESOURCES)

	# install icons
	install(
		DIRECTORY ${CMAKE_BINARY_DIR}/Resources/Icons/hicolor/
		DESTINATION ${ZEROSPADES_INSTALL_ICONS}
		FILES_MATCHING PATTERN "*.png")

	install(FILES Resources/Icons/zerospades.xpm DESTINATION ${ZEROSPADES_INSTALL_PIXMAP}/)

	# binary file
	install(PROGRAMS ${CMAKE_BINARY_DIR}/bin/zerospades DESTINATION ${ZEROSPADES_INSTALL_BINARY})

	# desktop file
	install(FILES Resources/Unix/Desktop/zerospades.desktop DESTINATION ${ZEROSPADES_INSTALL_DESKTOP}/)

	# Currently there is no files in lib/zerospades. But if you reading this message in future and want to
	# place somewhere libs, binaries or other non-media stuff, place them in lib/zerospades. Thank you.

endif(UNIX AND NOT APPLE)

if(APPLE)
	# Configure Info.plist with CMake variables
	configure_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/XSpades/XSpades-Info.plist.in"
		"${CMAKE_CURRENT_BINARY_DIR}/XSpades-Info.plist"
		@ONLY
	)

	# Create a macOS application bundle
	set_target_properties(ZeroSpades PROPERTIES
		MACOSX_BUNDLE ON
		MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/XSpades-Info.plist
		MACOSX_BUNDLE_BUNDLE_VERSION ${ZEROSPADES_VERSION_FULL}
		MACOSX_BUNDLE_LONG_VERSION_STRING ${ZEROSPADES_VERSION_FULL}
		MACOSX_BUNDLE_SHORT_VERSION_STRING ${ZEROSPADES_VERSION_FULL}
		MACOSX_BUNDLE_GUI_IDENTIFIER io.zerospades.ZeroSpades
		MACOSX_BUNDLE_BUNDLE_NAME ZeroSpades)

	set(BUNDLE_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icons/ZeroSpades.icns)
	list(APPEND BUNDLE_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
	if(ZEROSPADES_RESOURCES)
		list(APPEND BUNDLE_RESOURCES ${PAK_FILES})
		set_source_files_properties(${PAK_FILES}
			PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
		add_dependencies(ZeroSpades ZeroSpades_Resources_DevPaks ZeroSpades_Resources)
	endif(ZEROSPADES_RESOURCES)

	set_target_properties(ZeroSpades PROPERTIES RESOURCE "${BUNDLE_RESOURCES}")
	target_sources(ZeroSpades PRIVATE "${BUNDLE_RESOURCES}")
endif(APPLE)

#see http://www.cmake.org/cmake/help/v2.8.11/cpack.html
set(CPACK_PACKAGE_NAME "ZeroSpades")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Open-source clone of Ace of Spades")
set(CPACK_PACKAGE_DESCRIPTION "Open-source clone of Ace of Spades - free FPS sandbox game")
set(CPACK_PACKAGE_VENDOR "yvt, assorted")
set(CPACK_PACKAGE_CONTACT "yvt <i@yvt.jp>")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "zerospades-${ZEROSPADES_VERSION_FULL}")
set(CPACK_PACKAGE_FILE_NAME "zerospades-${ZEROSPADES_VERSION_FULL}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ZeroSpades ${ZEROSPADES_VERSION_FULL}")
set(CPACK_PACKAGE_VERSION_MAJOR ${ZEROSPADES_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${ZEROSPADES_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${ZEROSPADES_VERSION_PATCH})
set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_TOPLEVEL_TAG "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

if(WIN32 AND NOT UNIX)

	set(CPACK_NSIS_DISPLAY_NAME "ZeroSpades ${ZEROSPADES_VERSION_FULL}")
	# There is a bug in NSIS that does not handle full unix paths properly. Make
	# sure there is at least one set of four (4) backlasshes.
	set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\zerospades.exe")
	# TODO: set(CPACK_PACKAGE_ICON	"")
	set(CPACK_NSIS_HELP_LINK "https://sites.google.com/a/yvt.jp/openspades/")
	set(CPACK_NSIS_URL_INFO_ABOUT "http://buildandshoot.com/viewforum.php?f=57")
	set(CPACK_NSIS_CONTACT "i@yvt.jp")
	# TODO: set(CPACK_NSIS_CREATE_ICONS_EXTRA "")
	# TODO: set(CPACK_NSIS_DELETE_ICONS_EXTRA "")
	# TODO: set(CPACK_NSIS_MUI_FINISHPAGE_RUN "zerospades.exe")
	set(CPACK_NSIS_MODIFY_PATH ON)

elseif(UNIX AND NOT APPLE)

	# usage: `cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release .. ; make package`
	INCLUDE(InstallRequiredSystemLibraries)
	set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Open-source clone of Ace of Spades - free FPS sandbox game\n ZeroSpades is a fork of OpenSpades, a clone of Voxlap Ace of Spades 0.75, which is a free \n multiplayer game created by Ben Aksoy.\n .\n Here are some features:\n .\n   - Ability to connect to vanilla/pyspades server\n   - Better performance on some modern computers/netbooks\n	  - Sophisticated graphics\n   - Fully open source (GPLv3 compliant)\n	 - Cross-platform")
	set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Unix/Control/postinst)
	set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
	set(CPACK_STRIP_FILES "bin/zerospades")
	set(CPACK_SET_DESTDIR "on")
	set(CPACK_GENERATOR "DEB;TGZ")
	set(CPACK_DEBIAN_PACKAGE_DEPENDS
		"libc6 (>= 2.19), libgcc1 (>= 1:4.9), libstdc++6 (>= 4.8.2), zlib1g (>= 1:1.2.8), libcurl3 (>= 7.35.0), libglew1.10 (>= 1.10.0), libsdl2-2.0-0 (>= 2.0.2), libsdl2-image-2.0-0 (>= 2.0.0), libfreetype6 (>= 2.4.0)") # TODO: add libopus/libopusfile
	set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
	set(CPACK_DEBIAN_PACKAGE_SECTION "games")
	set(CPACK_DEBIAN_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})

	set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "")
	set(CPACK_DEBIAN_PACKAGE_SUGGESTS "")
	set(CPACK_COMPONENTS_ALL ALL)

endif(WIN32 AND NOT UNIX)

set(CPACK_PACKAGE_EXECUTABLES "zerospades" "Launch ZeroSpades")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY FALSE)		#everyone hates a dir inside a zip named exactly like the zip
include(CPack)

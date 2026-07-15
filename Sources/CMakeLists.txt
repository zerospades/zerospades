set(OS_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB AUDIO_FILES Audio/*.cpp Audio/*.h)
file(GLOB AUDIO_AL_FILES Audio/AL/*.cpp Audio/AL/*.h)
set(BINPACK_FILES binpack2d/binpack2d.hpp)
file(GLOB CLIENT_FILES Client/*.cpp Client/*.h)
file(GLOB CORE_FILES Core/*.c Core/*.cpp Core/*.h)
file(GLOB DRAW_GL_FILES Draw/OpenGL/*.cpp Draw/OpenGL/*.h)
file(GLOB DRAW_SW_FILES Draw/SW/*.cpp Draw/SW/*.h)
file(GLOB ENET_FILES ENet/*.c ENet/*.h)
file(GLOB ENET_INCLUDE ENet/include/enet/*.h)
file(GLOB GUI_FILES Gui/*.cpp Gui/*.h)
file(GLOB_RECURSE GUI_UI_FILES Gui/UI/*.cpp Gui/UI/*.h)
file(GLOB IMPORTS_FILES Imports/*.h)
file(GLOB KISS_FILES kiss_fft130/*.c kiss_fft130/*.h)
file(GLOB JSON_FILES json/*.cpp json/*.h json/*.inl)
file(GLOB JSON_INCLUDE json/include/json/*.h)
file(GLOB SCRIPTBINDING_FILES ScriptBindings/*.cpp ScriptBindings/*.h)
file(GLOB UNZIP_FILES unzip/*.c unzip/*.h)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	set_source_files_properties(
		${ENET_FILES} ${KISS_FILES} ${JSON_FILES} ${UNZIP_FILES}
		PROPERTIES COMPILE_OPTIONS "-w")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	set_source_files_properties(Client/GunCasing.cpp PROPERTIES COMPILE_OPTIONS "-Wno-array-bounds")
endif()

if(APPLE)
	file(GLOB PLATFORM_FILES Core/*.mm)
endif()

add_subdirectory(AngelScript/projects/cmake)
add_subdirectory(AngelScript/projects/cmake_addons)
set(ANGELSCRIPT_LIBS Angelscript Angelscript_addons)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	foreach(ANGELSCRIPT_TARGET Angelscript Angelscript_addons)
		if(TARGET ${ANGELSCRIPT_TARGET})
			target_compile_options(${ANGELSCRIPT_TARGET} PRIVATE -w)
		endif()
	endforeach()
endif()

include_directories(BEFORE ".")
include_directories(BEFORE "json/include")
include_directories(BEFORE "ENet/include")

if(WIN32)
	if(MINGW)	# = gcc, can someone confirm that this works?
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ZeroSpades_rc.o COMMAND windres.exe -I${CMAKE_CURRENT_BINARY_DIR}
			-i${CMAKE_CURRENT_BINARY_DIR}/ZeroSpades.rc -o ${CMAKE_CURRENT_BINARY_DIR}/ZeroSpades_rc.o)
		SET(RESOURCE_FILES ${CMAKE_CURRENT_BINARY_DIR}/ZeroSpades_rc.o)
	else()
		set(RESOURCE_FILES ${PROJECT_BINARY_DIR}/ZeroSpades.rc)	# msvc
	endif()
endif()

add_executable(ZeroSpades
	${AUDIO_FILES} ${AUDIO_AL_FILES} ${BINPACK_FILES} ${CLIENT_FILES}
	${CORE_FILES} ${PLATFORM_FILES} ${DRAW_GL_FILES} ${DRAW_SW_FILES} ${ENET_FILES}
	${ENET_INCLUDE} ${GUI_FILES} ${GUI_UI_FILES} ${IMPORTS_FILES} ${KISS_FILES}
	${JSON_FILES} ${JSON_INCLUDE} ${UNZIP_FILES} ${SCRIPTBINDING_FILES}
	${RESOURCE_FILES})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(ZeroSpades PRIVATE
		$<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-unknown-pragmas -Wno-unused-parameter -Wno-cast-function-type>
		$<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wno-unknown-pragmas -Wno-unused-parameter>)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	target_compile_options(ZeroSpades PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-sfinae-incomplete>)
endif()

set_target_properties(ZeroSpades PROPERTIES
	LINKER_LANGUAGE CXX
	OUTPUT_NAME zerospades
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
	LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
	ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(WIN32)
	set_target_properties(ZeroSpades PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set(GIT_COMMIT_HASH "?")

execute_process(
	COMMAND git log -1 --format=%h
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	OUTPUT_VARIABLE GIT_COMMIT_HASH
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

target_compile_definitions(ZeroSpades PRIVATE GIT_COMMIT_HASH="${GIT_COMMIT_HASH}")

if(APPLE OR WIN32)
	# This value is reflected to the macOS application bundle's name, so use
	# the correct capitalization.
	# We also want the correct capitalization on Windows.
	set_target_properties(ZeroSpades PROPERTIES OUTPUT_NAME ZeroSpades)
else()
	set_target_properties(ZeroSpades PROPERTIES OUTPUT_NAME zerospades)
endif()

if (APPLE)
	# The built pak files are copied into the macOS application bundle. CMake
	# won't copy unless they are included in the target's source files.
	#
	# Since they are specified as `add_custom_command`'s output files, so they
	# have the `GENERATED` property. However, `GENERATED` is a directory-local
	# property (until [cmake!5308] lands in CMake 3.20). This means it needs to
	# be set again here.
	#
	# [cmake!5308]: https://gitlab.kitware.com/cmake/cmake/-/merge_requests/5308
	set_source_files_properties(${PAK_FILES} PROPERTIES GENERATED 1)

endif(APPLE)

if(WIN32)
	# Use a single output directory for all configs
	# (Without this, the generated binary cannot find pak files created by mkpak.ps1 unless
	# a user explicitly specifies a search directory)
	foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
		set_target_properties(ZeroSpades PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/bin)
		set_target_properties(ZeroSpades PROPERTIES LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/bin)
		set_target_properties(ZeroSpades PROPERTIES ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/bin)
	endforeach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
endif()

add_dependencies(ZeroSpades Angelscript Angelscript_addons)

if(WIN32)
	source_group("Resources" ${RESOURCE_FILES})
	foreach(LIB ${SDL2_LIBRARY} ${SDL2_IMAGE_LIBRARY})
		string(REGEX REPLACE "\\.lib$" ".dll" SDL_DLL ${LIB})
		if(EXISTS "${SDL_DLL}")
			add_custom_command(
				TARGET ZeroSpades
				POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${SDL_DLL}"
					"${CMAKE_BINARY_DIR}/bin/\$\(Configuration\)/"
			)
			install(PROGRAMS ${SDL_DLL} DESTINATION bin)
		endif()
	endforeach()
	add_definitions(-DCURL_STATICLIB)
	get_filename_component(GLEWLIBFILE ${GLEW_LIBRARY} NAME_WE)
	if("glew32s" STREQUAL ${GLEWLIBFILE})
		add_definitions(-DGLEW_STATIC)	#static linked
	endif()
	if(MSVC)
		#lm: this sucks, msvc fucks up some linking (conflicts)
		set_target_properties(
			ZeroSpades PROPERTIES
			LINK_FLAGS_RELEASE
			"/NODEFAULTLIB:\"msvcrt.lib\" /NODEFAULTLIB:\"libcmtd.lib\" /NODEFAULTLIB:\"msvcrtd.lib\""
		)
	endif()
endif()

source_group("Audio" FILES ${AUDIO_FILES})
source_group("Audio\\AL" FILES ${AUDIO_AL_FILES})
source_group("libs\\binpack2d" FILES ${BINPACK_FILES})
source_group("Client" FILES ${CLIENT_FILES})
source_group("Core" FILES ${CORE_FILES})
source_group("Platform-specific" FILES ${PLATFORM_FILES})
source_group("Draw\\OpenGL" FILES ${DRAW_GL_FILES})
source_group("Draw\\SW" FILES ${DRAW_SW_FILES})
source_group("libs\\Enet" FILES ${ENET_FILES})
source_group("libs\\Enet\\include" FILES ${ENET_INCLUDE})
source_group("Gui" FILES ${GUI_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Gui/UI PREFIX "Gui\\UI" FILES ${GUI_UI_FILES})
source_group("Imports" FILES ${IMPORTS_FILES})
source_group("libs\\kiss_fft130" FILES ${KISS_FILES})
source_group("libs\\json" FILES ${JSON_FILES})
source_group("libs\\json\\include" FILES ${JSON_INCLUDE})
source_group("ScriptBindings" FILES ${SCRIPTBINDING_FILES})
source_group("libs\\unzip" FILES ${UNZIP_FILES})

target_link_libraries(ZeroSpades
	${SDL2_LIBRARY} ${SDL2_IMAGE_LIBRARY} ${OPENGL_LIBRARIES} ${ZLIB_LIBRARIES}
	${CURL_LIBRARY} ${FREETYPE_LIBRARIES} ${CMAKE_DL_LIBS} ${ANGELSCRIPT_LIBS}
	${Ogg_LIBRARY} ${OpusFile_LIBRARY})
if(NOT APPLE)
	target_link_libraries(ZeroSpades ${GLEW_LIBRARY})
endif()
if(USE_VCPKG)
	target_link_libraries(ZeroSpades Ogg::ogg Opus::opus)
endif()

#todo: MACOSX_BUNDLE_ICON_FILE ?

if(OPENAL_FOUND)
	target_link_libraries(ZeroSpades ${OPENAL_LIBRARY})
	include_directories(${OPENAL_INCLUDE_DIRS})
	# Define OPENAL_SOFT to indicate we're using OpenAL Soft
	target_compile_definitions(ZeroSpades PRIVATE OPENAL_SOFT=1)
endif()

# Copy OpenAL32.dll into the executable directory
if(WIN32 AND USE_INTERNAL_OPENAL)
	add_custom_command(TARGET ZeroSpades POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_FILE:OpenAL>
			$<TARGET_FILE_DIR:ZeroSpades>)
endif()

if(WIN32)
	target_link_libraries(ZeroSpades ws2_32.lib winmm.lib)
elseif(UNIX AND NOT APPLE)
	target_link_libraries(ZeroSpades Xext)
endif()

if(UNIX)
	if(NOT(CMAKE_SYSTEM_NAME MATCHES "BSD" OR APPLE))
		if (NOT CMAKE_SYSTEM_NAME MATCHES "Haiku")
			target_link_libraries(ZeroSpades rt)
		else()
			target_link_libraries(ZeroSpades network)
		endif()
	endif()
	target_link_libraries(ZeroSpades pthread)
endif()

# macOS-specific post-build steps
if(APPLE)
	# Copy OpenAL Soft dylib into the bundle and fix paths
	add_custom_command(TARGET ZeroSpades POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:ZeroSpades>/Frameworks"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_BINARY_DIR}/openal-soft/libopenal.1.dylib"
			"$<TARGET_BUNDLE_CONTENT_DIR:ZeroSpades>/Frameworks/"
		COMMAND install_name_tool -change "@rpath/libopenal.1.dylib"
			"@executable_path/../Frameworks/libopenal.1.dylib"
			"$<TARGET_FILE:ZeroSpades>"
		COMMENT "Embedding OpenAL Soft into macOS bundle"
	)

	# Add optional code signing (can be configured via MACOSX_CODESIGN_IDENTITY)
	# To sign: cmake -DMACOSX_CODESIGN_IDENTITY="Developer ID Application: Your Name" ..
	# Note: Code signing must happen AFTER all binary modifications (install_name_tool, etc.)
	if(MACOSX_CODESIGN_IDENTITY)
		add_custom_command(TARGET ZeroSpades POST_BUILD
			COMMAND codesign --force --deep --sign "${MACOSX_CODESIGN_IDENTITY}" "$<TARGET_BUNDLE_DIR:ZeroSpades>"
			COMMENT "Code signing macOS bundle with identity: ${MACOSX_CODESIGN_IDENTITY}"
		)
	else()
		# Ad-hoc signing for local development (allows the app to run without Gatekeeper issues)
		add_custom_command(TARGET ZeroSpades POST_BUILD
			COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:ZeroSpades>" || echo "Warning: Code signing failed"
			COMMENT "Ad-hoc code signing macOS bundle for local development"
		)
	endif()
endif()

#install(TARGETS ZeroSpades DESTINATION bin)

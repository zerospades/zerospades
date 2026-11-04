set(COMPILED_PAK_FILES
	${CMAKE_CURRENT_BINARY_DIR}/pak002-Base.pak
	${CMAKE_CURRENT_BINARY_DIR}/pak005-Models.pak
	${CMAKE_CURRENT_BINARY_DIR}/pak010-BaseSkin.pak
	${CMAKE_CURRENT_BINARY_DIR}/pak050-Locales.pak
	${CMAKE_CURRENT_BINARY_DIR}/pak999-References.pak)

if(ZEROSPADES_RESOURCES AND ZEROSPADES_NONFREE_RESOURCES)
	set(NONFREE_PAK_FILES
		${CMAKE_CURRENT_BINARY_DIR}/pak000-Nonfree.pak
		${CMAKE_CURRENT_BINARY_DIR}/font-unifont.pak)
endif(ZEROSPADES_RESOURCES AND ZEROSPADES_NONFREE_RESOURCES)

set(PAK_FILES
	${COMPILED_PAK_FILES}
	${NONFREE_PAK_FILES}
	PARENT_SCOPE)

if(ZEROSPADES_RESOURCES)
	if(ZEROSPADES_NONFREE_RESOURCES)
		if (WIN32)
			# No automatic downloading for Windows (for now)
		elseif (UNIX)
			add_custom_target(ZeroSpades_Resources_DevPaks ALL COMMENT "Downloading non-GPL assets"
				DEPENDS
					${CMAKE_CURRENT_BINARY_DIR}/pak000-Nonfree.pak
					${CMAKE_CURRENT_BINARY_DIR}/font-unifont.pak)
			add_custom_command(
				COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/downloadpak.sh
				OUTPUT
					${CMAKE_CURRENT_BINARY_DIR}/pak000-Nonfree.pak
					${CMAKE_CURRENT_BINARY_DIR}/font-unifont.pak
				WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
		endif()
	endif(ZEROSPADES_NONFREE_RESOURCES)

	file(GLOB_RECURSE GFX_FILES ${CMAKE_CURRENT_SOURCE_DIR}/Gfx/*)
	file(GLOB_RECURSE LICENSE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/License/*)
	file(GLOB_RECURSE LOCALE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/Locales/*)
	file(GLOB_RECURSE MAP_FILES ${CMAKE_CURRENT_SOURCE_DIR}/Maps/*.vxl)
	file(GLOB_RECURSE MODEL_FILES ${CMAKE_CURRENT_SOURCE_DIR}/Models/*)
	file(GLOB_RECURSE SCRIPT_FILES ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/*.as)
	file(GLOB_RECURSE SHADER_FILES ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.fs
		${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.glsl
		${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.program
		${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.vs)
	file(GLOB_RECURSE SOUND_FILES ${CMAKE_CURRENT_SOURCE_DIR}/Sounds/*)
	file(GLOB_RECURSE TEXTURE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/Textures/*)
	set(PAK_IN_FILES
		${GFX_FILES} ${LICENSE_FILES} ${LOCALE_FILES} ${MAP_FILES}
		${MODEL_FILES} ${SCRIPT_FILES} ${SHADER_FILES} ${SOUND_FILES}
		${TEXTURE_FILES})

	source_group("Gfx" FILES ${GFX_FILES})
	source_group("License Texts" FILES ${LICENSE_FILES})
	source_group("Translations" FILES ${LOCALE_FILES})
	source_group("Maps" FILES ${MAP_FILES})
	source_group("Models" FILES ${MODEL_FILES})
	source_group("Scripts" FILES ${SCRIPT_FILES})
	source_group("Shaders" FILES ${SHADER_FILES})
	source_group("Sounds" FILES ${SOUND_FILES})
	source_group("Textures" FILES ${TEXTURE_FILES})

	add_custom_target(ZeroSpades_Resources ALL
		COMMENT "Building assets"
		SOURCES ${PAK_IN_FILES}
		DEPENDS ${COMPILED_PAK_FILES})

	if(WIN32)
		add_custom_command(
			OUTPUT ${COMPILED_PAK_FILES}
			COMMAND powershell -ExecutionPolicy Bypass
				${CMAKE_CURRENT_SOURCE_DIR}/mkpak.ps1
				${CMAKE_CURRENT_BINARY_DIR}
			COMMENT "Building paks"
			DEPENDS ${PAK_IN_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/mkpak.ps1
			WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
	elseif (UNIX)
		add_custom_command(
			OUTPUT ${COMPILED_PAK_FILES}
			COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/mkpak.sh
			COMMENT "Building paks"
			DEPENDS ${PAK_IN_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/mkpak.sh
			WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
	endif()

	if (WIN32 AND MSVC)
		# copy the output files to the multi-config binary directory
		# (you can't use generator expressions (`$<...>`) in `add_custom_command`'s
		#  `OUTPUT` parameter; it's not supported by CMake.)
		add_custom_command(
			TARGET ZeroSpades_Resources POST_BUILD
			COMMENT "Copying assets to the currently-selected configuration's build directory"
			COMMAND
				${CMAKE_COMMAND} -E make_directory
					${CMAKE_BINARY_DIR}/bin/$<CONFIGURATION>/Resources
			COMMAND
				${CMAKE_COMMAND} -E copy_if_different
					${COMPILED_PAK_FILES}
					${CMAKE_BINARY_DIR}/bin/$<CONFIGURATION>/Resources)
	endif (WIN32 AND MSVC)
endif()

if(UNIX AND NOT APPLE)
	add_custom_target(ZeroSpades_Unix ALL COMMENT "Process unix stuff")

	file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Icons)
	add_custom_command(
		TARGET ZeroSpades_Unix
		POST_BUILD
		COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Icons/converticons.sh ${CMAKE_CURRENT_SOURCE_DIR}/Icons
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Icons)

	file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Unix)
	file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Unix/Debian)
	file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Unix/Man)

	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Unix/Debian/copyright.in" "${CMAKE_CURRENT_BINARY_DIR}/Unix/Debian/copyright")
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Unix/Man/zerospades.6.in" "${CMAKE_CURRENT_BINARY_DIR}/Unix/Man/zerospades.6")

	add_custom_command(
		TARGET ZeroSpades_Unix
		POST_BUILD
		COMMAND sh -c \"gzip -9 -c ${CMAKE_CURRENT_SOURCE_DIR}/Unix/Debian/changelog > changelog.gz\"
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Unix/Debian
	)
	add_custom_command(
		TARGET ZeroSpades_Unix
		POST_BUILD
		COMMAND sh -c \"gzip -9 -c ${CMAKE_CURRENT_BINARY_DIR}/Unix/Man/zerospades.6 > zerospades.6.gz\"
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Unix/Man
	)
endif(UNIX AND NOT APPLE)

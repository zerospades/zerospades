set(VCPKG_BUILD_TYPE release)
set(VCPKG_LIBRARY_LINKAGE dynamic)

vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/KhronosGroup/MoltenVK/releases/download/v${VERSION}/MoltenVK-macos.tar"
    FILENAME "MoltenVK-macos-${VERSION}.tar"
    SHA512 a394329f390a11d1052d2efeef5f7f33860465eea4d7e9e1faf808e616d533a283b9c55074fb833ef95ef40067fc319470eab07dc72ac63e6464dc26cbe34905
)

vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

# Only install MoltenVK-specific headers; vulkan/ and vk_video/ come from the vulkan-headers package
file(INSTALL "${SOURCE_PATH}/MoltenVK/include/MoltenVK"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include"
)

# Dynamic library
file(INSTALL "${SOURCE_PATH}/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib"
)

# ICD manifest (needed if the Vulkan loader is used instead of direct linking)
file(INSTALL "${SOURCE_PATH}/MoltenVK/dynamic/dylib/macOS/MoltenVK_icd.json"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/molten-vk"
)

# License
file(INSTALL "${SOURCE_PATH}/LICENSE"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/molten-vk"
    RENAME copyright
)

# CMake config — creates MoltenVK::MoltenVK imported target
file(WRITE "${CURRENT_PACKAGES_DIR}/share/molten-vk/molten-vk-config.cmake" [=[
if(TARGET MoltenVK::MoltenVK)
    return()
endif()
get_filename_component(_mvk_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_mvk_root "${_mvk_dir}/../.." ABSOLUTE)
add_library(MoltenVK::MoltenVK SHARED IMPORTED)
set_target_properties(MoltenVK::MoltenVK PROPERTIES
    IMPORTED_LOCATION "${_mvk_root}/lib/libMoltenVK.dylib"
    INTERFACE_INCLUDE_DIRECTORIES "${_mvk_root}/include"
)
unset(_mvk_dir)
unset(_mvk_root)
]=])

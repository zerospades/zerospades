cmake_minimum_required(VERSION 3.13)
project(AndroidAudioStartupRegression LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
if(NOT OPENAL_INCLUDE_DIR)
    message(FATAL_ERROR "Set OPENAL_INCLUDE_DIR to OpenAL Soft's include directory")
endif()
add_executable(openal_soft_resolver OpenALSoftResolverTest.cpp TestCoreStubs.cpp
    "${ROOT}/Sources/Audio/ALFuncs.cpp")
target_compile_definitions(openal_soft_resolver PRIVATE OPENAL_SOFT)
target_include_directories(openal_soft_resolver PRIVATE "${ROOT}/Sources" "${OPENAL_INCLUDE_DIR}")
# Link only referenced sections so the isolated test doesn't need a real OpenAL device.
target_compile_options(openal_soft_resolver PRIVATE -ffunction-sections -fdata-sections)
target_link_options(openal_soft_resolver PRIVATE -Wl,--gc-sections)
add_executable(dynamic_library DynamicLibraryTest.cpp TestCoreStubs.cpp
    "${ROOT}/Sources/Core/DynamicLibrary.cpp")
target_include_directories(dynamic_library PRIVATE "${ROOT}/Sources" "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(dynamic_library PRIVATE dl)
enable_testing()
add_test(NAME openal_soft_resolver COMMAND openal_soft_resolver)
add_test(NAME dynamic_library COMMAND dynamic_library)
# Optional integration check against an actual OpenAL Soft build on a host.
if(OPENAL_LIBRARY)
    add_executable(openal_soft_integration OpenALSoftIntegrationTest.cpp TestCoreStubs.cpp
        "${ROOT}/Sources/Audio/ALFuncs.cpp")
    target_compile_definitions(openal_soft_integration PRIVATE OPENAL_SOFT)
    target_include_directories(openal_soft_integration PRIVATE "${ROOT}/Sources" "${OPENAL_INCLUDE_DIR}")
    target_link_libraries(openal_soft_integration PRIVATE "${OPENAL_LIBRARY}")
    add_test(NAME openal_soft_integration COMMAND openal_soft_integration)
endif()
# Exercise the unchanged dynamic-driver bootstrap and EFX lookup too.
if(OPENAL_LIBRARY)
    add_executable(openal_dynamic_integration OpenALSoftIntegrationTest.cpp TestCoreStubs.cpp
        SDLBasePathStub.cpp "${ROOT}/Sources/Audio/ALFuncs.cpp" "${ROOT}/Sources/Core/DynamicLibrary.cpp")
    target_include_directories(openal_dynamic_integration PRIVATE "${ROOT}/Sources"
        "${OPENAL_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
    target_link_libraries(openal_dynamic_integration PRIVATE dl)
    add_test(NAME openal_dynamic_integration COMMAND openal_dynamic_integration)
    set_tests_properties(openal_dynamic_integration PROPERTIES ENVIRONMENT "ZS_TEST_OPENAL_PATH=${OPENAL_LIBRARY}")
endif()

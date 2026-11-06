cmake_minimum_required(VERSION 3.10)
project(AngelScriptNativeSmoke LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
add_subdirectory("${REPO_ROOT}/Sources/AngelScript/projects/cmake" angelscript)
add_subdirectory("${REPO_ROOT}/Sources/AngelScript/projects/cmake_addons" angelscript_addons)
add_executable(angelscript_native_smoke AngelScriptNativeSmoke.cpp)
target_include_directories(angelscript_native_smoke PRIVATE
    "${REPO_ROOT}/Sources/AngelScript/include" "${REPO_ROOT}/Sources/AngelScript/addons")
target_link_libraries(angelscript_native_smoke PRIVATE Angelscript_addons Angelscript)

# An executable alone doesn't verify that the static archives can be linked
# into the shared library the SDL game will eventually require.
if(ANDROID)
    add_library(angelscript_shared_link_check SHARED AngelScriptNativeSmoke.cpp)
    target_include_directories(angelscript_shared_link_check PRIVATE
        "${REPO_ROOT}/Sources/AngelScript/include" "${REPO_ROOT}/Sources/AngelScript/addons")
    target_link_libraries(angelscript_shared_link_check PRIVATE Angelscript_addons Angelscript)
    set_target_properties(angelscript_shared_link_check PROPERTIES LINK_FLAGS "-Wl,--no-undefined")
endif()

enable_testing()
if(NOT ANDROID)
    add_test(NAME angelscript_native_smoke COMMAND angelscript_native_smoke)
endif()

cmake_minimum_required(VERSION 3.14)

include(FetchContent)

# ---------------------------------------------------------------------------
# GoogleTest 1.17.0 — pinned by URL tarball (not git clone)
# Threat T-01-01: exact URL + DOWNLOAD_EXTRACT_TIMESTAMP prevent re-download attacks
# ---------------------------------------------------------------------------
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # Windows MSVC ABI compat
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# nlohmann/json 3.12.0 — vcpkg fallback for offline builds
# Threat T-01-02: exact URL + DOWNLOAD_EXTRACT_TIMESTAMP
# ---------------------------------------------------------------------------
if(DEFINED VCPKG_TARGET_TRIPLET)
    find_package(nlohmann_json CONFIG REQUIRED)
else()
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(nlohmann_json)
endif()

include(GoogleTest)

# ---------------------------------------------------------------------------
# zerospades_tests executable
# HitTestDebuggerStub.cpp satisfies the linker without pulling SWRenderer/SDL.
# Placeholder test is replaced by real test files in Plans 02-04.
# ---------------------------------------------------------------------------
add_executable(zerospades_tests
    Harness/HeadlessWorldTest.cpp
    Helpers/HitTestDebuggerStub.cpp
    Helpers/MakeFlatMap.cpp
    Helpers/RecordingWorldListener.cpp
    Helpers/HeadlessWorld.cpp
)

target_link_libraries(zerospades_tests PRIVATE
    zerospades_testable
    GTest::gtest_main
    GTest::gmock
    nlohmann_json::nlohmann_json
)

target_include_directories(zerospades_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Helpers
    ${CMAKE_SOURCE_DIR}/Sources
    ${CMAKE_SOURCE_DIR}/Sources/ENet/include
)

# FP-determinism flags on test binary (same as zerospades_testable)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(zerospades_tests PRIVATE
        -fno-fast-math -ffp-contract=off -msse2 -mfpmath=sse)
elseif(MSVC)
    target_compile_options(zerospades_tests PRIVATE /fp:strict)
endif()

gtest_discover_tests(zerospades_tests
    PROPERTIES LABELS "harness"
)

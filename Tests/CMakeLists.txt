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
    Harness/FPProbeTest.cpp
    Helpers/HitTestDebuggerStub.cpp
    Helpers/MakeFlatMap.cpp
    Helpers/RecordingWorldListener.cpp
    Helpers/HeadlessWorld.cpp
    Helpers/ToleranceMatchersTest.cpp
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

# Absolute Tests/ path for the in-process EXPECT_FLOAT_EQ ban guard so its
# scan does not depend on the test runner's working directory (D-17).
target_compile_definitions(zerospades_tests PRIVATE
    TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
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

# ---------------------------------------------------------------------------
# EXPECT_FLOAT_EQ ban (D-17 / SCHE-05 / threat T-02-12)
# Standalone CTest lint: fails if the banned matcher is *invoked* anywhere under
# Tests/. The regex matches the banned token immediately followed by optional
# whitespace then an open paren (the bracket class [(] is used instead of a
# literal paren so the ban-enforcement code, comments, and grep strings that
# only name the bare token do not self-trigger). The `! grep ...` inverts
# grep's exit code — passes (0) only when no invocation is found.
# Dual coverage with TEST(ToleranceMatchers, NoExpectFloatEqInTestsDir).
# ---------------------------------------------------------------------------
add_test(NAME NoExpectFloatEq
    COMMAND ${CMAKE_COMMAND} -E env
            sh -c "! grep -rE 'EXPECT_FLOAT_EQ[[:space:]]*[(]' ${CMAKE_SOURCE_DIR}/Tests/"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

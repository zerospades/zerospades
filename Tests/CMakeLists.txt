cmake_minimum_required(VERSION 3.14)

include(FetchContent)

# ---------------------------------------------------------------------------
# GoogleTest 1.17.0 — pinned by URL tarball (not git clone)
# Threat T-01-01: exact URL + URL_HASH SHA256 verify tarball content (supply-chain
# integrity). DOWNLOAD_EXTRACT_TIMESTAMP only controls CMake timestamp caching; the
# URL_HASH is what makes a compromised CDN / MITM / stale-content swap fail the build.
# SHA256 computed from the official v1.17.0.tar.gz release archive.
# ---------------------------------------------------------------------------
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # Windows MSVC ABI compat
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# nlohmann/json 3.12.0 — vcpkg fallback for offline builds
# Threat T-01-02: exact URL + URL_HASH SHA256 verify tarball content.
# SHA256 computed from the official v3.12.0 json.tar.xz release archive.
# ---------------------------------------------------------------------------
if(DEFINED VCPKG_TARGET_TRIPLET)
    find_package(nlohmann_json CONFIG REQUIRED)
else()
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
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
    Helpers/ContractDriftTest.cpp
    Helpers/PlayerPhysicsSnapshot.cpp
    Helpers/GrenadeSnapshot.cpp
    Protocol/ProtocolRoundTripTest.cpp
    Protocol/ProtocolReplay.cpp
    Protocol/ProtocolReplayUnitTest.cpp
    Protocol/ProtocolGoldenTest.cpp
    Protocol/ProtocolExtensionTest.cpp
    Protocol/ProtocolMalformedTest.cpp
    Physics/PhysicsFixtureTest.cpp
    Physics/PhysicsDeterminismTest.cpp
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
# CONTRACT_PATH is the absolute path to the portable contract; ContractDriftTest
# reads its §2 tolerance block and asserts it matches the oracle constants (D-06).
target_compile_definitions(zerospades_tests PRIVATE
    TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    CONTRACT_PATH="${CMAKE_SOURCE_DIR}/fixtures/CONTRACT.md"
)

# FP-determinism flags on test binary (same as zerospades_testable)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(zerospades_tests PRIVATE
        -fno-fast-math -ffp-contract=off -msse2 -mfpmath=sse)
elseif(MSVC)
    target_compile_options(zerospades_tests PRIVATE /fp:strict)
endif()

add_subdirectory(Map)
add_subdirectory(Weapons)
add_subdirectory(Grenade)
add_subdirectory(GameModes)

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
# Dual coverage with TEST(ToleranceMatchers, NoExpectFloatEqInTestsDir) which
# is the authoritative cross-platform guard; this CTest is a POSIX-only
# belt-and-suspenders check (sh is not available in native Windows MSVC builds).
# ---------------------------------------------------------------------------
if(NOT WIN32)
    add_test(NAME NoExpectFloatEq
        COMMAND ${CMAKE_COMMAND} -E env
                sh -c "! grep -rE 'EXPECT_FLOAT_EQ[[:space:]]*[(]' '${CMAKE_SOURCE_DIR}/Tests/'"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

# ---------------------------------------------------------------------------
# compare_snapshots C++ CLI (SCHE-06, D-19)
# Field-level WorldSnapshot diff; inherits the already-fetched
# nlohmann_json::nlohmann_json target declared above (no re-declaration).
# ---------------------------------------------------------------------------
if(ZEROSPADES_TESTS)
    add_subdirectory(${CMAKE_SOURCE_DIR}/tools/compare_snapshots
                     ${CMAKE_BINARY_DIR}/tools/compare_snapshots)

    # Smoke: identical file → exit 0 ("PASS")
    # Use $<TARGET_FILE:> so the test resolves the binary's real (possibly
    # per-config) path regardless of the test runner's working directory.
    add_test(NAME CompareSnapshotsMatch
             COMMAND $<TARGET_FILE:compare_snapshots>
                     ${CMAKE_SOURCE_DIR}/fixtures/phys_friction_ground_001.json
                     ${CMAKE_SOURCE_DIR}/fixtures/phys_friction_ground_001.json)
    set_tests_properties(CompareSnapshotsMatch PROPERTIES PASS_REGULAR_EXPRESSION "PASS")

    # Smoke: wrong arg count → exit 2 (usage error)
    add_test(NAME CompareSnapshotsUsage
             COMMAND $<TARGET_FILE:compare_snapshots>)
    set_tests_properties(CompareSnapshotsUsage PROPERTIES WILL_FAIL TRUE)

    # PROT-03 success criterion: exercise the portable compare_snapshots runner
    # against a committed proto golden fixture. Self-compare (oracle vs itself)
    # proves the runner walks the proto world_snapshot shape and reports PASS,
    # mirroring CompareSnapshotsMatch above.
    add_test(NAME CompareSnapshotsProtoGolden075
             COMMAND $<TARGET_FILE:compare_snapshots>
                     ${CMAKE_SOURCE_DIR}/fixtures/proto_golden_075_001.json
                     ${CMAKE_SOURCE_DIR}/fixtures/proto_golden_075_001.json)
    set_tests_properties(CompareSnapshotsProtoGolden075 PROPERTIES PASS_REGULAR_EXPRESSION "PASS")
endif()

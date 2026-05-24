# ---------------------------------------------------------------------------
# Tests/Map — block state, raycast, block action, cluster, and block-line
# fixture tests for the GameMap / World subsystem (Phase 6, MAP-01..MAP-04).
#
# Organisation mirrors Tests/Physics/: source files are added to the
# zerospades_tests executable defined in the parent Tests/CMakeLists.txt via
# target_sources (no separate executable — gtest_discover_tests is already
# called once at the parent level for the whole zerospades_tests target).
#
# Wave 1+ test files (added in Plans 02-04):
#   Map/BlockStateTest.cpp   — MAP-01 (IsSolid, GetColor, GetSolidMap, HasNeighbors, IsSurface)
#   Map/RaycastTest.cpp      — MAP-02 (CastRay2, startSolid edge, ClipBox/ClipWorld z-boundary)
#   Map/BlockActionTest.cpp  — MAP-03 (CreateBlock, DestroyBlock, post-action snapshot)
#   Map/ClusterTest.cpp      — MAP-04 (BlocksFell multi-callback, 3 cluster scenarios)
#   Map/BlockLineTest.cpp    — MAP-05 (CubeLine cell list + post-fill solid state)
#
# This file is intentionally empty of target_sources calls until Wave 1 plans
# populate the test files above.  The subdirectory is included unconditionally
# by the parent so CMake processes it on every configure.
# ---------------------------------------------------------------------------

# Wave 1 — Plan 02: MAP-01 block state fixtures (IsSolid, GetColor, GetSolidMap,
# HasNeighbors, IsSurface).
target_sources(zerospades_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/BlockStateTest.cpp
)

# Wave 2 — Plan 03: MAP-02 raycast/collision fixtures (CastRay2 hit/miss/startSolid,
# ClipBox/ClipWorld z-boundary semantics).
target_sources(zerospades_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/RaycastTest.cpp
)

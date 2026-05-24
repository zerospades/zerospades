# ---------------------------------------------------------------------------
# Tests/Grenade — deterministic N-tick grenade trajectory golden fixture tests
# for the Grenade subsystem (Phase 7, WEAP-03).
#
# Organisation mirrors Tests/Map/ and Tests/Physics/: source files are added to
# the zerospades_tests executable defined in the parent Tests/CMakeLists.txt via
# target_sources (no separate executable — gtest_discover_tests is already
# called once at the parent level for the whole zerospades_tests target).
#
#   Grenade/GrenadeTrajectoryTest.cpp — WEAP-03 (gravity + floor/wall bounce
#                                       trajectory, water landing z>=63, fuse
#                                       countdown -> Explode; world_snapshot +
#                                       step_trace, GrenadeSnapshot per tick)
# ---------------------------------------------------------------------------

target_sources(zerospades_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/GrenadeTrajectoryTest.cpp
)

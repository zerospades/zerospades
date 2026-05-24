# ---------------------------------------------------------------------------
# Tests/Weapons — weapon damage/spread constant and fire/ammo/reload timing
# fixture tests for the Weapon subsystem (Phase 7, WEAP-01..WEAP-02).
#
# Organisation mirrors Tests/Map/ and Tests/Physics/: source files are added to
# the zerospades_tests executable defined in the parent Tests/CMakeLists.txt via
# target_sources (no separate executable — gtest_discover_tests is already
# called once at the parent level for the whole zerospades_tests target).
#
#   Weapons/WeaponDamageTest.cpp — WEAP-01 (6 variants × 5 HitType damage +
#                                  spread/pellet/clip/reload constants, value_lookup)
#   Weapons/WeaponTimingTest.cpp — WEAP-02 (fire-rate gate, ammo decrement,
#                                  empty-clip no-fire, reload timing — step_trace;
#                                  spread/recoil modifier rules — value_lookup)
# ---------------------------------------------------------------------------

target_sources(zerospades_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/WeaponDamageTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/WeaponTimingTest.cpp
)

# ---------------------------------------------------------------------------
# Tests/GameModes — CTF/TC game-mode state and kill/leave protocol-fold
# characterization tests, driven through the ProtocolReplay accumulator
# (Phase 7, MODE-01..MODE-03 + CR-02).
#
# Organisation mirrors Tests/Map/ and Tests/Physics/: source files are added to
# the zerospades_tests executable defined in the parent Tests/CMakeLists.txt via
# target_sources (no separate executable — gtest_discover_tests is already
# called once at the parent level for the whole zerospades_tests target).
#
#   GameModes/CTFFixtureTest.cpp     — MODE-01 + MODE-03 CTF (intel pickup/drop/
#                                       capture/restock, capture limit; world golden)
#   GameModes/TCFixtureTest.cpp      — MODE-02 + MODE-03 TC (ownership change,
#                                       pinned-clock GetProgress, progress bar)
#   GameModes/KillLeaveFoldTest.cpp  — CR-02 (KillAction alive=false + killer
#                                       score++ self-kill guard; PlayerLeft erase +
#                                       no-resurrection; dead-player health=0)
# ---------------------------------------------------------------------------

target_sources(zerospades_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/CTFFixtureTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TCFixtureTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/KillLeaveFoldTest.cpp
)

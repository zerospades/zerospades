# compare_snapshots — C++ CLI WorldSnapshot field-level diff (SCHE-06, D-19).
# Gated behind ZEROSPADES_TESTS. Inherits the already-fetched
# nlohmann_json::nlohmann_json target from Tests/CMakeLists.txt scope
# (this file is processed via add_subdirectory from there) — DO NOT
# re-declare FetchContent for nlohmann_json or googletest here.

if(ZEROSPADES_TESTS)
    add_executable(compare_snapshots
        ${CMAKE_CURRENT_LIST_DIR}/main.cpp
    )
    target_link_libraries(compare_snapshots PRIVATE
        zerospades_testable
        nlohmann_json::nlohmann_json
    )
    target_include_directories(compare_snapshots PRIVATE
        ${CMAKE_SOURCE_DIR}/Tests/Helpers
        ${CMAKE_SOURCE_DIR}/Sources
        ${CMAKE_SOURCE_DIR}/Sources/ENet/include
    )
    # FP-determinism flags (same as zerospades_tests / zerospades_testable)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(compare_snapshots PRIVATE
            -fno-fast-math -ffp-contract=off)
        if(ZEROSPADES_USE_SSE_FP_FLAGS)
            target_compile_options(compare_snapshots PRIVATE -msse2 -mfpmath=sse)
        endif()
    elseif(MSVC)
        target_compile_options(compare_snapshots PRIVATE /fp:strict)
    endif()
endif()

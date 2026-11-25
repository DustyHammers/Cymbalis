# cymbalis_bench — CPU cost of the plugin's real-time engine (FastEngine).

add_executable(cymbalis_bench main.cpp Dense.cpp Kit.cpp EnginePool74.cpp)
target_link_libraries(cymbalis_bench PRIVATE cymbalis_fast cymbalis_pool)
# --kit: several cymbals through the engine pool (Kit.cpp), checked against #74's pool
# (EnginePool74.cpp, a renamed copy kept for the bench only). Not in the profile build, whose
# part timers are global, for one engine on one thread.
target_compile_definitions(cymbalis_bench PRIVATE CYMBALIS_BENCH_KIT=1)

# cymbalis_bench_profile — the same, with FastEngine's parts timed (cymbalis_fast_profile):
# --dense then also reports where strike() and the slowest blocks spend their time.
add_executable(cymbalis_bench_profile main.cpp Dense.cpp)
target_link_libraries(cymbalis_bench_profile PRIVATE cymbalis_fast_profile)

foreach(target cymbalis_bench cymbalis_bench_profile)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
    if(APPLE)
        target_compile_options(${target} PRIVATE "SHELL:-Xarch_x86_64 -msse4.1")
    endif()
endforeach()

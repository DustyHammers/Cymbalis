# cymbalis_bench — CPU cost of the plugin's real-time engine (FastEngine).

add_executable(cymbalis_bench main.cpp Dense.cpp)
target_link_libraries(cymbalis_bench PRIVATE cymbalis_fast)

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

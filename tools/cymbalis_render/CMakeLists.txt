# cymbalis_render — console renderer for the A/B comparison pipeline.

add_executable(cymbalis_render main.cpp)
target_link_libraries(cymbalis_render PRIVATE cymbalis_engine cymbalis_fast)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cymbalis_render PRIVATE -ffp-contract=off -fno-fast-math -Wall -Wextra)
endif()
if(APPLE)
    target_compile_options(cymbalis_render PRIVATE "SHELL:-Xarch_x86_64 -msse4.1")
endif()

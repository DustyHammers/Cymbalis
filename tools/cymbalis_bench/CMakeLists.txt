# cymbalis_bench — CPU cost of the plugin's real-time engine (FastEngine).

add_executable(cymbalis_bench main.cpp)
target_link_libraries(cymbalis_bench PRIVATE cymbalis_fast)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cymbalis_bench PRIVATE -Wall -Wextra)
endif()
if(APPLE)
    target_compile_options(cymbalis_bench PRIVATE "SHELL:-Xarch_x86_64 -msse4.1")
endif()

# cymbalis_trims — measures each slot's level trim with FastEngine, the plugin's engine.

add_executable(cymbalis_trims main.cpp)
target_link_libraries(cymbalis_trims PRIVATE cymbalis_fast)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cymbalis_trims PRIVATE -Wall -Wextra)
endif()
if(APPLE)
    target_compile_options(cymbalis_trims PRIVATE "SHELL:-Xarch_x86_64 -msse4.1")
endif()

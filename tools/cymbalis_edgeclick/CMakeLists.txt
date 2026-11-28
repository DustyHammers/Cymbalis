# cymbalis_edgeclick — no click at the hand-over from an EDGE strike's contact to its free phase,
# in memory, with FastEngine, the plugin's engine (docs/PORT_MAP.md, "An envelope strike's contact
# plays at its envelope level"). CI runs it on every build.

add_executable(cymbalis_edgeclick main.cpp)
target_link_libraries(cymbalis_edgeclick PRIVATE cymbalis_fast)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cymbalis_edgeclick PRIVATE -Wall -Wextra)
endif()
if(APPLE)
    target_compile_options(cymbalis_edgeclick PRIVATE "SHELL:-Xarch_x86_64 -msse4.1")
endif()

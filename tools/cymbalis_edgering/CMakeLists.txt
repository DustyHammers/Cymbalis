# cymbalis_edgering — an EDGE strike's ring kept when another strike follows, in memory, with
# FastEngine, the plugin's engine (docs/PORT_MAP.md, "An envelope strike keeps its ring when
# another strike follows"). CI runs it on every build.

add_executable(cymbalis_edgering main.cpp)
target_link_libraries(cymbalis_edgering PRIVATE cymbalis_fast)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cymbalis_edgering PRIVATE -Wall -Wextra)
endif()
if(APPLE)
    target_compile_options(cymbalis_edgering PRIVATE "SHELL:-Xarch_x86_64 -msse4.1")
endif()

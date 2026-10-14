# cymbalis_engine — the C++ port of the Python V19 engine (no JUCE dependency).

add_library(cymbalis_engine STATIC
    Json.cpp
    Bake.cpp
    Engine.cpp
)

target_include_directories(cymbalis_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cymbalis_engine PUBLIC cxx_std_17)
set_target_properties(cymbalis_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The engine reproduces numpy's float64 arithmetic one operation at a time.
# Without -ffp-contract=off, the compiler may fuse a*b+c into one FMA, which
# rounds differently from numpy's separate multiply and add.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cymbalis_engine PRIVATE -ffp-contract=off -fno-fast-math -Wall -Wextra)
endif()

# Same rule as the plugin: the x86_64 slice never uses AVX.
if(APPLE)
    target_compile_options(cymbalis_engine PRIVATE "SHELL:-Xarch_x86_64 -msse4.1")
endif()

# cymbalis_fast — the real-time engine used by the plugin (FastEngine.h). It needs the
# bake loader from cymbalis_engine but none of its bit-exactness rules, so it is built
# with FP contraction allowed (FMA on arm64).
add_library(cymbalis_fast STATIC
    FastEngine.cpp
)
target_link_libraries(cymbalis_fast PUBLIC cymbalis_engine)
set_target_properties(cymbalis_fast PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cymbalis_fast PRIVATE -O3 -ffp-contract=fast -fno-fast-math -Wall -Wextra)
endif()
if(APPLE)
    target_compile_options(cymbalis_fast PRIVATE "SHELL:-Xarch_x86_64 -msse4.1")
endif()

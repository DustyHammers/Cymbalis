# dhpool — a real-time job pool for audio plugins (README-pool.md). Plain C++17, no JUCE, no
# dependency on the project that includes it: add_subdirectory(shared/dhpool) and link dhpool.

find_package(Threads REQUIRED)

add_library(dhpool STATIC
    JobPool.cpp
)
target_include_directories(dhpool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(dhpool PUBLIC cxx_std_17)
target_link_libraries(dhpool PUBLIC Threads::Threads)
set_target_properties(dhpool PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(dhpool PRIVATE -O3 -fno-fast-math -Wall -Wextra)
endif()
if(APPLE)
    target_compile_options(dhpool PRIVATE "SHELL:-Xarch_x86_64 -msse4.1")
endif()

# dhpool_selftest — dummy jobs at 1-4 threads (test/selftest.cpp). Exit status 1 on a failure.
option(DHPOOL_BUILD_TESTS "Build dhpool's self-test" ON)
if(DHPOOL_BUILD_TESTS)
    add_executable(dhpool_selftest test/selftest.cpp)
    target_link_libraries(dhpool_selftest PRIVATE dhpool)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(dhpool_selftest PRIVATE -O2 -fno-fast-math -Wall -Wextra)
    endif()
    if(APPLE)
        target_compile_options(dhpool_selftest PRIVATE "SHELL:-Xarch_x86_64 -msse4.1")
    endif()
endif()

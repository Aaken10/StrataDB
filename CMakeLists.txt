cmake_minimum_required(VERSION 3.15)
project(StrataDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(strata_core
    src/arena.cpp
    src/skip_list.cpp
    src/memtable.cpp
    src/wal.cpp
    src/bloom_filter.cpp
    src/sstable.cpp
)

target_include_directories(strata_core PUBLIC include)

find_package(Threads REQUIRED)
target_link_libraries(strata_core PUBLIC Threads::Threads)

option(ENABLE_TESTS "Enable building tests" ON)
if(ENABLE_TESTS)
    enable_testing()
    add_executable(test_skiplist tests/test_skiplist.cpp)
    target_link_libraries(test_skiplist PRIVATE strata_core)
    add_test(NAME skiplist_test COMMAND test_skiplist)
    add_executable(test_memtable tests/test_memtable.cpp)
    target_link_libraries(test_memtable PRIVATE strata_core)
    add_test(NAME memtable_test COMMAND test_memtable)
    add_executable(test_sstable tests/test_sstable.cpp)
    target_link_libraries(test_sstable PRIVATE strata_core)
    add_test(NAME sstable_test COMMAND test_sstable)
endif()

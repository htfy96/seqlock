cmake_minimum_required(VERSION 3.2)
project(SeqLock CXX)

# From https://github.com/rigtorp/Seqlock/blob/master/CMakeLists.txt

add_library(SeqLock INTERFACE)
target_include_directories(SeqLock INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

find_package(Threads REQUIRED)

enable_testing()
add_executable(test-seqlock test/test.cpp)
target_link_libraries(test-seqlock SeqLock Threads::Threads)
set_target_properties(test-seqlock
    PROPERTIES
    CXX_STANDARD 11
    CXX_EXTENSIONS OFF)
add_test(test-seqlock test-seqlock)

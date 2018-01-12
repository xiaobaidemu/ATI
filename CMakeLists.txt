cmake_minimum_required(VERSION 3.1 FATAL_ERROR)

project(Sendrecv CXX)

# Enable C++ 11
set(CMAKE_CXX_STANDARD 11)

# Add src to include directory
include_directories("src")
include_directories(".")

# Link with thread library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
link_libraries(Threads::Threads)

# Include all header files under src/common/
file(GLOB COMMON_FILES "src/common/*.cpp" "src/common/*.h" "src/common/*.hpp")
file(GLOB TESTS_FILES "tests/*.cpp" "tests/*.h" "tests/*.hpp")
file(GLOB SRC_FILES "src/*.cpp" "src/*.h" "src/*.hpp")

set(SOURCE_FILES
    ${COMMON_FILES}
    ${TESTS_FILES}
    ${SRC_FILES}
)

add_executable(Sendrecv ${SOURCE_FILES})

cmake_minimum_required(VERSION 3.1 FATAL_ERROR)

project(Sendrecv CXX)

# Enable C++ 11
set(CMAKE_CXX_STANDARD 11)
add_definitions("-Wall -g")

# Add src to include directory
include_directories("src")
include_directories(".")

# Link with thread library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
link_libraries(Threads::Threads ibverbs rdmacm)

# Include all header files under src/common/
file(GLOB COMMON_FILES "src/common/*.cpp" "src/common/*.h" "src/common/*.hpp" "src/common/safemap.h")
file(GLOB TESTS_FILES "tests/*.cpp" "tests/*.h" "tests/*.hpp")
file(GLOB SRC_FILES "src/*.cpp" "src/*.h" "src/*.hpp" "src/rdma_src/*.cpp" "src/rdma_src/*.h" "src/rdma_src/conn_system.cpp")


set(SOURCE_FILES
    ${COMMON_FILES}
    ${SRC_FILES}
)


add_executable(two_process_small ${SOURCE_FILES} tests_init/two_process_small.cpp)
add_executable(init_n_process ${SOURCE_FILES} tests_init/init_n_process.cpp)
add_executable(two_process_large ${SOURCE_FILES} tests_init/two_process_large.cpp)
add_executable(two_process_mix ${SOURCE_FILES} tests_init/two_process_mix.cpp)

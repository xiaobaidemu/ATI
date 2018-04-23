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
file(GLOB SRC_FILES "src/*.cpp" "src/*.h" "src/*.hpp" "src/rdma_src/*.cpp" "src/rdma_src/*.h"
        "src/rdma_src/conn_system.cpp" "src/tcp_src/*.h" "src/tcp_src/*.cpp")

set(SOURCE_FILES
    ${COMMON_FILES}
    ${SRC_FILES}
)

add_executable(test_uniform_normal ${SOURCE_FILES} test_uniform_API/test_uniform_normal.cpp)
add_executable(test_tcp_normal ${SOURCE_FILES} tests_tcp/test_tcp_normal.cpp)
add_executable(test_tcp_init_n ${SOURCE_FILES} tests_tcp/test_tcp_init_n.cpp)
add_executable(test_tcp_speed  ${SOURCE_FILES} tests_tcp/test_tcp_speed.cpp)
add_executable(test_tcp_alltoall  ${SOURCE_FILES} tests_tcp/test_tcp_alltoall.cpp)
add_executable(test_tcp_normal_2  ${SOURCE_FILES} tests_tcp/test_tcp_normal_2.cpp)
add_executable(test_tcp_mix_diff_size  ${SOURCE_FILES} tests_tcp/test_tcp_mix_diff_size.cpp)
#add_executable(two_process_normal ${SOURCE_FILES} tests_rdma/two_process_normal.cpp)
#add_executable(two_process_normal_2 ${SOURCE_FILES} tests_rdma/two_process_normal_2.cpp)
#add_executable(two_process_oneside ${SOURCE_FILES} tests_rdma/two_process_oneside.cpp)
#add_executable(init_n_process ${SOURCE_FILES} tests_rdma/init_n_process.cpp)
#add_executable(two_process_large ${SOURCE_FILES} tests_rdma/two_process_large.cpp)
#add_executable(two_process_mix ${SOURCE_FILES} tests_rdma/two_process_mix.cpp)
#add_executable(two_process_mix_oneside ${SOURCE_FILES} tests_rdma/two_process_mix_oneside.cpp)
#add_executable(two_process_speed ${SOURCE_FILES} tests_rdma/two_process_speed.cpp)

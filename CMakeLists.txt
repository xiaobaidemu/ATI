cmake_minimum_required(VERSION 3.1 FATAL_ERROR)

project(Sendrecv CXX)

# Enable C++ 11
set(CMAKE_CXX_STANDARD 11)
#add_definitions("-w")
add_definitions("-Wall")

# Add src to include directory
include_directories("src")
include_directories(".")

# Link with thread library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_library(iblib_var NAMES ibverbs rdmacm)
if(iblib_var)
    message("ibverbs and rdmacm library found.")
    add_definitions(-DIBEXIST)
    link_libraries(Threads::Threads ibverbs rdmacm)
else()
    message("ibverbs and rdmacm library not found")
    link_libraries(Threads::Threads)
endif()


# Include all header files under src/common/
file(GLOB COMMON_FILES "src/common/*.cpp" "src/common/*.h" "src/common/*.hpp" "src/common/safemap.h")
file(GLOB TESTS_FILES "tests/*.cpp" "tests/*.h" "tests/*.hpp")
file(GLOB SRC_FILES "src/*.cpp" "src/*.h" "src/*.hpp" "src/rdma_src/*.cpp" "src/rdma_src/*.h"
        "src/rdma_src/conn_system.cpp" "src/tcp_src/*.h" "src/tcp_src/*.cpp")

set(SOURCE_FILES
    ${COMMON_FILES}
    ${SRC_FILES}
)
file(GLOB COMMON_HEADERS "src/common/*.h")
file(GLOB RDMA_SRC_HEADERS "src/rdma_src/*.h")
file(GLOB TCP_SRC_HEADERS "src/tcp_src/*.h")
file(GLOB COMM_HEADERS "src/*.h")

add_library(at_sendrecv SHARED ${SOURCE_FILES})
SET_TARGET_PROPERTIES (at_sendrecv PROPERTIES VERSION 0.1 SOVERSION 1)
INSTALL(FILES ${COMMON_HEADERS} DESTINATION include/at_sendrecv/common)
INSTALL(FILES ${RDMA_SRC_HEADERS} DESTINATION include/at_sendrecv/rdma_src)
INSTALL(FILES ${TCP_SRC_HEADERS} DESTINATION include/at_sendrecv/tcp_src)
INSTALL(FILES ${COMM_HEADERS} DESTINATION include/at_sendrecv/)

INSTALL(TARGETS at_sendrecv LIBRARY DESTINATION lib)

#cmake -DCMAKE_INSTALL_PREFIX=/xxx/xxx ..
#SET(CMAKE_INSTALL_PREFIX < install_path >)
#the method set the install directory
# g++ alltoall.cpp -o alltoall -I/usr/local/include/at_sendrecv -lat_sendrecv -lpthread

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

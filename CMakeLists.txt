cmake_minimum_required(VERSION 3.10)
project(OS_TRANSPORT VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 收集所有src下的.c文件（包含新增的线程池）
file(GLOB SRC_FILES src/*.c)

# 共享库目标
add_library(os_transport SHARED
    ${SRC_FILES}
)

# 链接pthread库（线程池依赖）
target_link_libraries(os_transport pthread)

target_include_directories(os_transport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(os_transport PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME "os_transport"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# 安装规则（不变）
install(
    TARGETS os_transport
    LIBRARY DESTINATION lib64
    COMPONENT runtime
)

install(
    FILES include/os_transport.h
          include/os_transport_thread_pool.h  # 新增：安装线程池公共头文件到devel包
    DESTINATION include/os-transport
    COMPONENT devel
)

# 可选：编译测试程序
add_executable(test_transport test/test_transport.c)
target_link_libraries(test_transport os_transport pthread)
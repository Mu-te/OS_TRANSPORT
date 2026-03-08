cmake_minimum_required(VERSION 3.10)
project(OS_TRANSPORT VERSION 1.0.0 LANGUAGES C)

# C语言标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 共享库目标
add_library(os_transport SHARED
    src/os_transport.c
)

# 头文件路径
target_include_directories(os_transport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 库属性（版本、输出名）
set_target_properties(os_transport PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME "os_transport"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ========== 关键：RPM打包需要的安装规则 ==========
# 1. 安装头文件到 /usr/include/os-transport
install(
    FILES include/os_transport.h
    DESTINATION include/os-transport
    COMPONENT devel
)

# 2. 安装共享库到 /usr/lib64（Linux标准64位库路径）
install(
    TARGETS os_transport
    LIBRARY DESTINATION lib64
    COMPONENT runtime
)
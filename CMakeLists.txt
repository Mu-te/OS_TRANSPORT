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
    VERSION ${PROJECT_VERSION}          # 完整版本：1.0.0
    SOVERSION ${PROJECT_VERSION_MAJOR}  # 主版本：1
    OUTPUT_NAME "os_transport"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ========== 核心修改：强制安装到lib64 ==========
# 1. 安装头文件到 /usr/include/os-transport
install(
    FILES include/os_transport.h
    DESTINATION include/os-transport
    COMPONENT devel
)

# 2. 强制安装库到 lib64（无论系统默认是lib还是lib64）
install(
    TARGETS os_transport
    LIBRARY DESTINATION lib64  # 强制指定lib64，匹配spec文件
    COMPONENT runtime
)
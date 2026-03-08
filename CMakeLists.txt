cmake_minimum_required(VERSION 3.10)
project(OS_TRANSPORT VERSION 1.0.0 LANGUAGES C)

# C语言标准（跨架构兼容）
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 共享库目标
add_library(os_transport SHARED
    src/os_transport.c
)

# 头文件路径（供编译时查找）
target_include_directories(os_transport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 库属性（版本、输出名，跨架构兼容）
set_target_properties(os_transport PROPERTIES
    VERSION ${PROJECT_VERSION}          # 完整版本：1.0.0
    SOVERSION ${PROJECT_VERSION_MAJOR}  # 主版本：1
    OUTPUT_NAME "os_transport"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ========== 1. 安装运行库（主包）：lib64目录 ==========
install(
    TARGETS os_transport
    LIBRARY DESTINATION lib64
    COMPONENT runtime
)

# ========== 2. 安装头文件（仅devel包）：include/os-transport目录 ==========
install(
    FILES include/os_transport.h
    DESTINATION include/os-transport
    COMPONENT devel
)
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(MyFileSystem)

# 设置 C++ 标准 (例如 C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 设置构建输出目录 (可选，但推荐)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 添加头文件目录
include_directories(
    include
    include/fs_core
    include/file_operations
    include/user_management
)

# fs_core 模块源文件
aux_source_directory(src/fs_core FS_CORE_SOURCES)

# file_operations 模块源文件
aux_source_directory(src/file_operations FILE_OPERATIONS_SOURCES)

# user_management 模块源文件
aux_source_directory(src/user_management USER_MANAGEMENT_SOURCES)

# 主源文件
set(MAIN_SOURCES
    src/filesystem.cpp
    src/shell.cpp
    src/main.cpp
)

# 将所有源文件集合到一个列表中
set(ALL_SOURCES
    ${FS_CORE_SOURCES}
    ${FILE_OPERATIONS_SOURCES}
    ${USER_MANAGEMENT_SOURCES}
    ${MAIN_SOURCES}
)

# 添加可执行文件目标
add_executable(${PROJECT_NAME} ${ALL_SOURCES})

# 如果有需要链接的库，可以在这里添加
# target_link_libraries(${PROJECT_NAME} PRIVATE some_library)

# 打印消息 (可选)
message(STATUS "Project Name: ${PROJECT_NAME}")
message(STATUS "CXX Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Source files: ${ALL_SOURCES}")
message(STATUS "Include directories: ${CMAKE_INCLUDE_PATH}")
message(STATUS "Output directory for executables: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
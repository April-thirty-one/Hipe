cmake_minimum_required(VERSION 3.20)

# 绑定版本为 C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARY_REQUIRED ON)
set(CMAKE_EXTENSIONS ON)
message("=== the C++ standard current used : C++${CMAKE_CXX_STANDARD} ===")

project(myHipe LANGUAGES CXX)
message("=== project name : ${PROJECT_NAME} ===")
message("=== project path is : ${PROJECT_SOURCE_DIR} ===")

# 构建模式：Debug
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
    message("=== the current mode is ${CMAKE_BUILD_TYPE} ===")
endif()

# 添加头文件
include_directories(${PROJECT_SOURCE_DIR}/include)

# 添加测试程序
add_subdirectory(${PROJECT_SOURCE_DIR}/test)

message("=== compile and link is complete. ===")

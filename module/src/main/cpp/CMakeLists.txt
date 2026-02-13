cmake_minimum_required(VERSION 3.22.1)
project(sample)

set(CXX_FLAGS "${CXX_FLAGS} -fno-exceptions -fno-rtti -fvisibility=hidden -fvisibility-inlines-hidden")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS}")

add_library(${MODULE_NAME} SHARED zygisk.cpp companion.cpp)

# 链接 log 库和 C++ 标准库（静态或共享均可，这里用共享）
target_link_libraries(${MODULE_NAME} log c++_shared)
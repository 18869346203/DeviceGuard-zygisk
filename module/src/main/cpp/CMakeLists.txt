cmake_minimum_required(VERSION 3.22.1)
project(sample)

set(CXX_FLAGS "${CXX_FLAGS} -fno-exceptions -fno-rtti -fvisibility=hidden -fvisibility-inlines-hidden")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS}")

add_library(${MODULE_NAME} SHARED zygisk.cpp companion.cpp)

# 只链接 log 库，不再需要 cxx 和 external 子目录
target_link_libraries(${MODULE_NAME} log)

# add_subdirectory(external)   ← 删掉或注释掉，不需要了
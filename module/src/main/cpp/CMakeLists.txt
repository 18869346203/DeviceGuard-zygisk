cmake_minimum_required(VERSION 3.22.1)
project(sample)

set(CXX_FLAGS "${CXX_FLAGS} -fno-exceptions -fno-rtti -fvisibility=hidden -fvisibility-inlines-hidden")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS}")

# 🚩 把 example.cpp 改成 zygisk.cpp companion.cpp
add_library(${MODULE_NAME} SHARED zygisk.cpp companion.cpp)

target_link_libraries(${MODULE_NAME} cxx log)

add_subdirectory(external)
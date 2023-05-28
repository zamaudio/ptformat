cmake_minimum_required(VERSION 3.18)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED true)

project(ptformat)

add_library(${PROJECT_NAME} ptformat.cc ptformat/ptformat.h)
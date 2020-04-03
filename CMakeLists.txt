cmake_minimum_required(VERSION 2.8)
project(fat12_demo)

message("\nThis is a demo of FAT12 file system emulation.\nCreated by jxzhn.\n")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-O3 -Wall")
include_directories(include/)
file(GLOB SRCS "src/*.c")
add_executable(${PROJECT_NAME} main.c ${SRCS})

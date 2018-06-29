cmake_minimum_required(VERSION 3.10)
project(tet C)

set(CMAKE_C_STANDARD 99)

add_executable(tet main.c tet.c tet.h main.c tet.c tet.h)

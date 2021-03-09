cmake_minimum_required(VERSION 3.17)
project(mcwrapper)

set(CMAKE_CXX_STANDARD 14)

find_package(Lua REQUIRED)
include_directories(${LUA_INCLUDE_DIRS})

set(HEADERS
        utils.h
        parser.h)

set(SOURCES
        main.cpp
        utils.cpp
        parser.cpp)

add_executable(mcwrapper ${SOURCES} ${HEADERS})
cmake_minimum_required(VERSION 3.1)
project(mcwrapper)

set(CMAKE_CXX_STANDARD 14)

find_package(Lua REQUIRED)
include_directories(${LUA_INCLUDE_DIRS})

find_package(libwebsockets REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(HEADERS
        utils.h
        parser.h
        ServerInfo.h
        webserv/http_server.h
        )

set(SOURCES
        main.cpp
        utils.cpp
        parser.cpp
        ServerInfo.cpp
        webserv/http_server.cpp
        )

add_executable(mcwrapper ${SOURCES} ${HEADERS})
target_link_libraries(mcwrapper
        PUBLIC websockets ${LIBWEBSOCKETS_DEP_LIBS}
        PRIVATE Threads::Threads
        )
CMAKE_MINIMUM_REQUIRED(VERSION 3.10)
PROJECT(Toriginal_OS LANGUAGES CXX)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable position independent code for userspace
SET(CMAKE_POSITION_INDEPENDENT_CODE ON)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

# Shell executable
SET(SHELL_SOURCES
    src/shell.cpp
    src/main.cpp
)

ADD_EXECUTABLE(toriginal_shell ${SHELL_SOURCES})

TARGET_INCLUDE_DIRECTORIES(toriginal_shell PRIVATE include)

# Installation
INSTALL(TARGETS toriginal_shell DESTINATION bin)

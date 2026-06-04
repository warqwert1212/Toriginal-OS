CMAKE_MINIMUM_REQUIRED(VERSION 3.10)
PROJECT(GUI LANGUAGES CXX)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# GUI Framework Library
ADD_LIBRARY(gui_framework STATIC
    src/gui_framework.cpp
    src/system_apps.cpp
)

TARGET_INCLUDE_DIRECTORIES(gui_framework PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Desktop Application
ADD_EXECUTABLE(desktop src/desktop.cpp)
TARGET_LINK_LIBRARIES(desktop gui_framework)

# Install targets
INSTALL(TARGETS desktop DESTINATION bin)
INSTALL(TARGETS gui_framework DESTINATION lib)
INSTALL(DIRECTORY include/ DESTINATION include)

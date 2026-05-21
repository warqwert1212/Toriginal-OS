CMAKE_MINIMUM_REQUIRED(VERSION 3.10)
PROJECT(Drivers LANGUAGES C)

SET(CMAKE_C_STANDARD 99)
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")

# Network Driver Library
ADD_LIBRARY(net_driver STATIC
    network/src/network_driver.c
)

TARGET_INCLUDE_DIRECTORIES(net_driver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/network/include
)

# Install targets
INSTALL(TARGETS net_driver DESTINATION lib)
INSTALL(DIRECTORY network/include/ DESTINATION include)

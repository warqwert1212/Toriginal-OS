CMAKE_MINIMUM_REQUIRED(VERSION 3.10)
PROJECT(freeNT_OS LANGUAGES C CXX ASM)

# Add subdirectories
ADD_SUBDIRECTORY(freeNT)
add_subdirectory(sys/shell)
add_subdirectory(sys/gui)
# add_subdirectory(sys/userpc)
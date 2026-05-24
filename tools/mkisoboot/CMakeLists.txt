cmake_minimum_required(VERSION 3.10)
project(mkisoboot C)

add_executable(mkisoboot mkisoboot.c)

install(TARGETS mkisoboot RUNTIME DESTINATION bin)

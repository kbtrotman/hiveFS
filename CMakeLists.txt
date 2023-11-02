cmake_minimum_required(VERSION 3.0.0)
project(HiveFS VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_REQUIRED_LIBRARIES "pq" "crypto" "fuse3")

include(CTest)
enable_testing()

add_executable(${PROJECT_NAME}   hfs.c
                                 hfs.h
)

target_include_directories(${PROJECT_NAME} PUBLIC 
   /home/trotmkeb/c/hiveFS/
   /usr/include/linux/
   /usr/src/kernels/4.18.0-425.13.1.el8_7.x86_64/include/ 
   #/usr/src/kernels/4.18.0-425.13.1.el8_7.x86_64/arch/x86/include/
   /usr/include/pq/
)

target_link_libraries(${PROJECT_NAME} PUBLIC pq crypto)

target_compile_options(${PROJECT_NAME} PUBLIC -Wall -pedantic-errors)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)


cmake_minimum_required(VERSION 2.8)
project(redis-network)
message(STATUS "this is binary dir" ${PROJECT_BINARY_DIR})
message(STATUS "this is source dir" ${PROJECT_SOURCE_DIR})
#设置编译选项
add_definitions("-g -fPIC  -pipe -fno-ident -MMD -Werror -D_GNU_SOURCE -D_REENTRANT ")

#set(CMAKE_CXX_FLAGS "-O2 -g -Wall -shared")
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

#设定include目录：
include_directories(${PROJECT_SOURCE_DIR})
include_directories( ./ ./utils ./include)


#设定link目录
include_directories(./lib ) 


#获取目录下的c/cpp文件合集
aux_source_directory(${PROJECT_SOURCE_DIR} DIR_SRCS)
aux_source_directory(./utils  UTILS_SRCS)
message(STATUS "this is DIR_SRCS " ${DIR_SRCS})
message(STATUS "this is UTILS_SRCS " ${UTILS_SRCS})


#编译成动态库
#add_library(main.so SHARED ${DIR_SRCS} ${UTILS_SRCS}) 

#编译成可执行文件
add_executable(main ${DIR_SRCS} ${UTILS_SRCS}) 

#添加链接库
#target_link_libraries(echo attrapi qos_client64 config++ sproto tcmalloc)
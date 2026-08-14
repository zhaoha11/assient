cmake_minimum_required(VERSION 3.20.0)

project(MyProject)

set(CMAKE_CXX_STANDARD 11)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
#静态库的输出路径
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
#动态库的输出路径
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
#可执行文件的输出路径
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)


add_subdirectory(${PROJECT_SOURCE_DIR}/EdoyunNet)
add_subdirectory(${PROJECT_SOURCE_DIR}/RtmpServer)
add_subdirectory(${PROJECT_SOURCE_DIR}/SigServer)
add_subdirectory(${PROJECT_SOURCE_DIR}/LoadBanceServer)
add_subdirectory(${PROJECT_SOURCE_DIR}/LoginServer)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
project(TCPSvr)

include_directories(${CMAKE_SOURCE_DIR}/TCPServer)

aux_source_directory(. TCP_SRC)

add_executable(
    TCPSvr
    ${TCP_SRC}
)

target_link_libraries(
    TCPSvr
    EdoyunNet
)


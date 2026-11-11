add_library(sr_ipc STATIC
  Protocol.cpp
  PipeChannel.cpp
)

target_link_libraries(sr_ipc PUBLIC sr_core)
target_include_directories(sr_ipc PUBLIC ${CMAKE_SOURCE_DIR}/src)

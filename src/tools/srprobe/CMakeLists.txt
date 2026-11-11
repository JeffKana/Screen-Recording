add_executable(srprobe main.cpp window_host.cpp)

target_link_libraries(srprobe PRIVATE sr_core sr_platform sr_capture sr_audio d3d11)

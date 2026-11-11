add_executable(srctl main.cpp)

target_link_libraries(srctl PRIVATE sr_core sr_ipc)

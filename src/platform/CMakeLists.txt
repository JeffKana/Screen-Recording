add_library(sr_platform STATIC
  win/QpcClock.cpp
)

target_link_libraries(sr_platform PUBLIC sr_core)
target_include_directories(sr_platform PUBLIC ${CMAKE_SOURCE_DIR}/src)

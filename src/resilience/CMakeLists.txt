add_library(sr_resilience STATIC
  MkvRecovery.cpp
)

target_link_libraries(sr_resilience PUBLIC sr_core)
target_include_directories(sr_resilience PUBLIC ${CMAKE_SOURCE_DIR}/src)

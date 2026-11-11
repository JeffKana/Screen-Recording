add_library(sr_core STATIC
  error/ErrorBus.cpp
  state/RecorderState.cpp
  util/DiskEstimate.cpp
  util/PathProbe.cpp
)

target_include_directories(sr_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

set_target_properties(sr_core PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

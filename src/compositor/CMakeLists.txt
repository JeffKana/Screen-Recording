add_library(sr_compositor STATIC
  Nv12Converter.cpp
)

target_link_libraries(sr_compositor PUBLIC sr_core sr_platform PRIVATE d3d11)

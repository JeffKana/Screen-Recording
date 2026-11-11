add_library(sr_mux STATIC
  MkvWriter.cpp
)

target_link_libraries(sr_mux PUBLIC sr_core)

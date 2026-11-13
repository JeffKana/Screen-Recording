add_library(sr_mux STATIC
  MkvWriter.cpp
  Mp4Writer.cpp
)

target_link_libraries(sr_mux PUBLIC sr_core)

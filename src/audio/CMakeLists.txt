add_library(sr_audio STATIC
  WasapiSource.cpp
  FloatMixer.cpp
  AudioTimeline.cpp
)

target_link_libraries(sr_audio
  PUBLIC sr_core sr_platform
  PRIVATE ole32 uuid
)

target_include_directories(sr_audio PUBLIC ${CMAKE_SOURCE_DIR}/src)

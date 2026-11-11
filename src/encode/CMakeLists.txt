add_library(sr_encode STATIC
  MfVideoEncoder.cpp
  MfAudioEncoder.cpp
  EncoderList.cpp
)

target_link_libraries(sr_encode PUBLIC sr_core sr_platform PRIVATE mfplat mfuuid ole32)

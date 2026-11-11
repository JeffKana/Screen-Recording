add_executable(sr-recorder
  main.cpp
  SessionServer.cpp
  RecordingSession.cpp
  StorageGuard.cpp
  OutputPath.cpp
)

target_link_libraries(sr-recorder PRIVATE
  sr_core
  sr_ipc
  sr_platform
  sr_capture
  sr_compositor
  sr_audio
  sr_encode
  sr_mux
  sr_resilience
  sr_diag
  d3d11
  # LT-01：GetProcessMemoryInfo 取 PeakWorkingSet64（内存基线不能靠外部采样猜）。
  psapi
)

if(SR_TEST_HOOKS)
  target_compile_definitions(sr-recorder PRIVATE SR_TEST_HOOKS=1)
endif()

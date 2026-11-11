# WGC 依赖 C++/WinRT，只有 MSVC 能编译；MinGW 下该源文件走空实现分支。
option(SR_ENABLE_WGC "Build Windows.Graphics.Capture backend (MSVC only)" ON)

add_library(sr_capture STATIC
  DxgiCaptureSource.cpp
  RegionCaptureSource.cpp
  WindowCaptureSource.cpp
  WgcCaptureSource.cpp
  CaptureChain.cpp
  CaptureDeviceAccess.cpp
  CaptureFactory.cpp
)

target_link_libraries(sr_capture
  PUBLIC sr_core sr_platform
  PRIVATE d3d11 dxgi dwmapi
)

if(MSVC AND SR_ENABLE_WGC)
  target_compile_definitions(sr_capture PRIVATE SR_HAS_WGC)
  target_link_libraries(sr_capture PRIVATE windowsapp)
  message(STATUS "sr_capture: WGC backend ENABLED (MSVC)")
else()
  message(STATUS "sr_capture: WGC backend DISABLED (fallback to DXGI/PrintWindow)")
endif()

target_include_directories(sr_capture PUBLIC ${CMAKE_SOURCE_DIR}/src)

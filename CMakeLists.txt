cmake_minimum_required(VERSION 3.25)
project(ScreenRecorder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# MinGW 只用于本地验证构建：全静态链接，避免验收脚本依赖 PATH 里的 DLL
# （仅 -static-libstdc++ 不够，POSIX 线程构建还需要 libwinpthread-1.dll）。
if(MINGW)
  add_link_options(-static)
  add_compile_options(-Wall -Wextra)
endif()

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

option(SR_BUILD_TOOLS "Build diagnostic CLI tools" ON)
# 轮 30（F3）：产品二进制里的测试注入开关（--preflight-fail / --set-* / --fail-* / --inject-drop-*）。
# 默认 ON 保持现有自动化可用；G6 打包时用 -DSR_TEST_HOOKS=OFF 出干净包，
# 此时 --help 也不再列出这些开关。
option(SR_TEST_HOOKS "Enable product test hooks (debug switches)" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/diag)
add_subdirectory(src/ipc)
add_subdirectory(src/capture)
add_subdirectory(src/audio)
add_subdirectory(src/compositor)
add_subdirectory(src/encode)
add_subdirectory(src/mux)
add_subdirectory(src/resilience)
add_subdirectory(src/recorder)
add_subdirectory(src/app)

if(SR_BUILD_TOOLS)
  add_subdirectory(src/tools/srctl)
  add_subdirectory(src/tools/srprobe)
endif()

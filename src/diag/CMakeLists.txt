# 轮 30：日志分级 + 环形缓冲 + 诊断包（M6-03 / M8-14）。
# 纯文本产物，不依赖任何采集/编码库。
add_library(sr_diag STATIC
    Logger.cpp
    DiagPack.cpp
)

target_include_directories(sr_diag PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sr_diag PUBLIC user32 advapi32 dxgi)

# 轮 27：Win32 原生 UI 骨架（S5）。
# 硬约束：本目标只链接 sr_ipc / sr_core；不得链接 capture/encode/mux，
# 录制一律通过 sr-recorder.exe 子进程 + 命名管道完成。
# 轮 30：新增 sr_diag（日志分级 + 诊断包），仍是纯文本，不碰采集/编码。
add_executable(sr-ui WIN32
    main.cpp
    AppConfig.cpp
    FileOps.cpp
    RecorderController.cpp
)

target_include_directories(sr-ui PRIVATE ${CMAKE_SOURCE_DIR}/src)
# per-monitor v2 / GetDpiForWindow 需要 Win10 目标；MinGW 默认头版本较旧。
target_compile_definitions(sr-ui PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006 WINVER=0x0A00)
# 轮 32（M5-06）：录制列表用 ListView，需要 comctl32（系统组件，不是三方依赖）。
target_link_libraries(sr-ui PRIVATE sr_ipc sr_core sr_diag user32 shell32 gdi32 ole32 mfplat mfuuid comctl32)

# F3：测试注入开关（--preflight-fail / --set-*）由宏隔离；打包时 -DSR_TEST_HOOKS=OFF。
if(SR_TEST_HOOKS)
    target_compile_definitions(sr-ui PRIVATE SR_TEST_HOOKS=1)
endif()

# MinGW 的 GUI 子系统默认找 WinMain；显式用 Unicode 入口以匹配 wWinMain。
if(MINGW)
    target_link_options(sr-ui PRIVATE -municode)
endif()

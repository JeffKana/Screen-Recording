// 轮 38 · M1-08/M1-09 验收支撑：自建「目标窗口 + 遮挡窗口」。
//
// 为什么不用记事本/浏览器：Win11 的记事本是打包应用，MainWindowHandle 取不到
// （g2_walk.ps1:151 已实测失败）。自建窗口可控、可缩放、可遮挡，
// 且颜色可断言（目标=纯绿，遮挡=纯红）。
//
// 用法：
//   srprobe windowhost [--width 800] [--height 600] [--seconds 20]
//                      [--resize-to 1280x720@8] [--occluder]
//                      [--hwnd-file <path>]
//
// 输出（stdout）：
//   windowhost_hwnd=0x........      目标窗口句柄
//   windowhost_size=800x600        初始客户区尺寸
//   windowhost_occluder=1          是否创建了遮挡窗口
//   windowhost_resized=1280x720    计划中的缩放目标（无则省略）
//
// --hwnd-file：把窗口句柄（十进制）写进文件。
// 用途（M1-27 轮 40）：以高完整性启动本进程时 stdout 拿不到（提权实例没有管道），
// 验收脚本要拿到目标窗口句柄才能让 recorder 去采它。

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

HWND g_target = nullptr;
HWND g_occluder = nullptr;
bool g_occluder_visible = false;

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        // 纯绿：验收脚本按「绿色像素占比」判定内容是否完整。
        HBRUSH brush = CreateSolidBrush(RGB(0, 255, 0));
        FillRect(dc, &rc, brush);
        DeleteObject(brush);
        // 蓝色边框：用来判断「内容是否被裁剪」——四边都在才说明整幅窗口内容都在画面里
        // （只画左上角时右边和下边的边框会消失）。
        {
            const int border = 6;
            RECT edges[4] = {
                {rc.left, rc.top, rc.right, rc.top + border},
                {rc.left, rc.bottom - border, rc.right, rc.bottom},
                {rc.left, rc.top, rc.left + border, rc.bottom},
                {rc.right - border, rc.top, rc.right, rc.bottom},
            };
            HBRUSH blue = CreateSolidBrush(RGB(0, 0, 255));
            for (const RECT& edge : edges) {
                FillRect(dc, &edge, blue);
            }
            DeleteObject(blue);
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 0, 0));
        DrawTextW(dc, L"SRPROBE", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        // 移动的黑色方块：让画面持续变化。
        // WGC 只在内容变化时才产帧；静止窗口不会产生新帧，时间轴会停住。
        const DWORD tick = GetTickCount();
        const int size = 48;
        const int span = (rc.right > rc.bottom) ? rc.right : rc.bottom;
        const int offset = static_cast<int>((tick / 20) % static_cast<DWORD>(span > size ? span - size : 1));
        RECT marker{offset, offset, offset + size, offset + size};
        HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(dc, &marker, black);
        DeleteObject(black);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK OccluderWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        // 纯红：若成片里出现红色，说明录到的是遮挡物而不是目标窗口。
        HBRUSH brush = CreateSolidBrush(RGB(255, 0, 0));
        FillRect(dc, &rc, brush);
        DeleteObject(brush);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void ParseResize(const std::string& spec, uint32_t& width, uint32_t& height, uint32_t& at_seconds) {
    const size_t at = spec.find('@');
    const size_t x = spec.find('x');
    if (at == std::string::npos || x == std::string::npos || x > at) {
        return;
    }
    width = static_cast<uint32_t>(std::strtoul(spec.substr(0, x).c_str(), nullptr, 10));
    height = static_cast<uint32_t>(std::strtoul(spec.substr(x + 1, at - x - 1).c_str(), nullptr, 10));
    at_seconds = static_cast<uint32_t>(std::strtoul(spec.substr(at + 1).c_str(), nullptr, 10));
}

int RunWindowHost(int argc, char** argv) {
    uint32_t width = 800;
    uint32_t height = 600;
    uint32_t seconds = 20;
    uint32_t resize_width = 0;
    uint32_t resize_height = 0;
    uint32_t resize_at = 0;
    // 轮 39（M1-08 崩溃验收）：每隔 N 秒在两个尺寸间来回切换（压「反复 resize」的路径）。
    uint32_t resize_cycle = 0;
    bool want_occluder = false;
    std::string hwnd_file;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--width" && i + 1 < argc) {
            width = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--height" && i + 1 < argc) {
            height = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--seconds" && i + 1 < argc) {
            seconds = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--resize-to" && i + 1 < argc) {
            ParseResize(argv[++i], resize_width, resize_height, resize_at);
        } else if (arg == "--resize-cycle" && i + 1 < argc) {
            resize_cycle = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--occluder") {
            want_occluder = true;
        } else if (arg == "--hwnd-file" && i + 1 < argc) {
            hwnd_file = argv[++i];
        }
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = HostWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SrProbeHostWindow";
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);

    WNDCLASSW oc{};
    oc.lpfnWndProc = OccluderWndProc;
    oc.hInstance = GetModuleHandleW(nullptr);
    oc.lpszClassName = L"SrProbeHostOccluder";
    oc.hbrBackground = nullptr;
    RegisterClassW(&oc);

    // 无边框弹出窗口：整个窗口都是客户区（没有系统标题栏，绿色区域完整可断言）。
    g_target = CreateWindowExW(WS_EX_TOPMOST,
                                L"SrProbeHostWindow",
                                L"srprobe host",
                                WS_POPUP | WS_VISIBLE,
                                100,
                                100,
                                static_cast<int>(width),
                                static_cast<int>(height),
                                nullptr,
                                nullptr,
                                GetModuleHandleW(nullptr),
                                nullptr);
    if (g_target == nullptr) {
        std::printf("windowhost=FAIL create_window error=%lu\n", GetLastError());
        return 2;
    }
    if (want_occluder) {
        g_occluder = CreateWindowExW(WS_EX_TOPMOST,
                                     L"SrProbeHostOccluder",
                                     L"srprobe occluder",
                                     WS_POPUP | WS_VISIBLE,
                                     100,
                                     100,
                                     static_cast<int>(width),
                                     static_cast<int>(height),
                                     nullptr,
                                     nullptr,
                                     GetModuleHandleW(nullptr),
                                     nullptr);
        g_occluder_visible = g_occluder != nullptr;
    }

    std::printf("windowhost_hwnd=0x%llx\n", static_cast<unsigned long long>(
                                               reinterpret_cast<uintptr_t>(g_target)));
    std::printf("windowhost_size=%ux%u\n", width, height);
    std::printf("windowhost_occluder=%d\n", g_occluder_visible ? 1 : 0);
    if (resize_width > 0 && resize_height > 0) {
        std::printf("windowhost_resized=%ux%u\n", resize_width, resize_height);
    }
    std::fflush(stdout);
    // M1-27：提权实例的 stdout 拿不到，句柄写文件给验收脚本。
    if (!hwnd_file.empty()) {
        if (std::FILE* file = std::fopen(hwnd_file.c_str(), "wb")) {
            const unsigned long long handle = static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(g_target));
            std::fprintf(file, "%llu\n", handle);
            std::fclose(file);
        } else {
            std::printf("windowhost_hwnd_file=FAIL\n");
            std::fflush(stdout);
        }
    }

    const DWORD started = GetTickCount();
    bool resized = false;
    MSG msg{};
    while (GetTickCount() - started < seconds * 1000) {
        // 持续重绘：WGC 只在内容变化时产帧，静止窗口会让录制时间轴停住。
        InvalidateRect(g_target, nullptr, FALSE);
        if (!resized && resize_width > 0 && resize_height > 0 &&
            GetTickCount() - started >= resize_at * 1000) {
            // 模拟「用户拖动窗口改变尺寸」：只改尺寸，位置不动。
            SetWindowPos(g_target, nullptr, 100, 100, static_cast<int>(resize_width),
                         static_cast<int>(resize_height), SWP_NOZORDER | SWP_NOACTIVATE);
            if (g_occluder != nullptr) {
                SetWindowPos(g_occluder, nullptr, 100, 100, static_cast<int>(resize_width),
                             static_cast<int>(resize_height), SWP_NOZORDER | SWP_NOACTIVATE);
            }
            resized = true;
            std::printf("windowhost_resized_at=%u\n", resize_at);
            std::fflush(stdout);
        } else if (resize_cycle > 0) {
            // 轮 39：来回切换尺寸（每 resize_cycle 秒一次），压「反复 resize」的路径。
            const uint32_t elapsed = (GetTickCount() - started) / 1000;
            const uint32_t phase = elapsed / resize_cycle;
            const uint32_t want_width = (phase % 2 == 0) ? resize_width : width;
            const uint32_t want_height = (phase % 2 == 0) ? resize_height : height;
            if (want_width > 0 && want_height > 0) {
                SetWindowPos(g_target, nullptr, 100, 100, static_cast<int>(want_width),
                             static_cast<int>(want_height), SWP_NOZORDER | SWP_NOACTIVATE);
                if (g_occluder != nullptr) {
                    SetWindowPos(g_occluder, nullptr, 100, 100, static_cast<int>(want_width),
                                 static_cast<int>(want_height), SWP_NOZORDER | SWP_NOACTIVATE);
                }
                std::printf("windowhost_cycle_at=%u size=%ux%u\n", elapsed, want_width, want_height);
                std::fflush(stdout);
            }
            std::fflush(stdout);
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(20);
    }

    if (g_occluder != nullptr) {
        DestroyWindow(g_occluder);
    }
    DestroyWindow(g_target);
    return 0;
}

}  // namespace

int RunWindowHostCommand(int argc, char** argv) {
    return RunWindowHost(argc, argv);
}

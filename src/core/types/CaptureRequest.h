#pragma once

#include <cstdint>

#include "core/types/Frame.h"

namespace sr {

enum class CaptureKind : uint8_t {
    Display = 0,  // 全屏 / 指定显示器（M1-01、M1-02）
    Window,       // 单窗口（M1-07）
    Region,       // 自定义区域（M1-04）
};

struct CaptureRequest {
    CaptureKind kind = CaptureKind::Display;
    uint32_t display_index = 0;
    // 平台窗口句柄。core 层只当作不透明数值传递（M6-13）。
    uint64_t window_handle = 0;
    Rect region{};
    bool include_cursor = true;   // M1-24
    uint32_t target_fps = 30;     // M3-03
    bool allow_gdi_fallback = true;  // 03 文档 §4 的三级路径
    // 强制指定后端（"wgc" / "dxgi" / "printwindow"）；nullptr = 按优先级自动选择。
    const char* preferred_backend = nullptr;
    // 恢复时排除某个后端，用于验证真实降级（C11）。nullptr = 不排除。
    const char* excluded_backend = nullptr;
};

struct SourceCaps {
    Size max_size{};
    uint32_t max_fps = 0;
    bool supports_cursor_toggle = false;
    // 被遮挡时仍能采集（M1-09）。GDI 兜底路径必须返回 false。
    bool supports_occlusion = false;
    // 后端标识：wgc / dxgi-dd / gdi，仅用于诊断与日志。
    const char* backend = "unknown";
};

}  // namespace sr

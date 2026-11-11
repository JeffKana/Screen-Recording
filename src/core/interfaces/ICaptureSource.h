#pragma once

#include <string>

#include "core/types/CaptureRequest.h"
#include "core/types/Frame.h"

namespace sr {

// 屏幕/窗口采集抽象。实现：WGC → DXGI Desktop Duplication → GDI BitBlt（03 文档 §4）。
// 契约要求：TryAcquire 绝不阻塞调用方（02 文档 §3 采集线程硬实时约束）。
class ICaptureSource {
public:
    virtual ~ICaptureSource() = default;

    virtual bool Start(const CaptureRequest& request, std::string& error) = 0;

    virtual void Stop() = 0;

    virtual bool IsRunning() const = 0;

    // 非阻塞取帧。返回 false 表示当前无新帧（不是错误）。
    // 返回 true 时填充 out，但 native_texture 仅在下次 TryAcquire 前有效。
    virtual bool TryAcquire(VideoFrame& out) = 0;

    virtual SourceCaps Caps() const = 0;

    // 采集源属性变化（分辨率、DPI、显示器断开）时置位，由上层触发重建（M8-10）。
    virtual bool NeedsRebuild() const = 0;
};

}  // namespace sr

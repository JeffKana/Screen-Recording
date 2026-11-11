#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/interfaces/ICaptureSource.h"

namespace sr::capture {

// 后端降级链（03 文档 §4 的三级路径）。
//
// 设计要点：
//  - 初始化失败 → 顺序尝试下一个后端（WGC → DXGI DD → PrintWindow）；
//  - 运行期 NeedsRebuild → 先原地重建一次，仍失败才切下一个后端；
//  - 每次降级都写进 trace()，供 UI 提示与诊断包使用（M8-14）。
class CaptureChain final : public ICaptureSource {
public:
    using BackendFactory = std::function<std::unique_ptr<ICaptureSource>()>;

    CaptureChain(std::vector<BackendFactory> backends, std::vector<std::string> names);

    bool Start(const CaptureRequest& request, std::string& error) override;
    void Stop() override;
    bool IsRunning() const override;
    bool TryAcquire(VideoFrame& out) override;
    SourceCaps Caps() const override;
    bool NeedsRebuild() const override;

    const std::string& ActiveBackend() const { return active_name_; }
    // 当前活跃后端，供 CaptureDeviceAccess 取设备。
    ICaptureSource* Current() const { return current_.get(); }
    // 形如 "wgc:fail(...) -> dxgi:ok"
    const std::string& Trace() const { return trace_; }

private:
    bool ActivateFrom(size_t index, std::string& error);

    std::vector<BackendFactory> backends_;
    std::vector<std::string> names_;
    std::unique_ptr<ICaptureSource> current_;
    CaptureRequest request_{};
    std::string excluded_;
    std::string active_name_ = "none";
    std::string trace_;
    size_t active_ = 0;
    bool running_ = false;
};

}  // namespace sr::capture

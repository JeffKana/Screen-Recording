#include "capture/CaptureDeviceAccess.h"

#include "capture/CaptureChain.h"
#include "capture/DxgiCaptureSource.h"
#include "capture/RegionCaptureSource.h"
#include "capture/WgcCaptureSource.h"

namespace sr::capture {

bool TryGetCaptureDevice(ICaptureSource* source, CaptureDevice& out) {
    if (source == nullptr) {
        return false;
    }

    // 降级链：递归到当前活跃的后端。
    if (auto* chain = dynamic_cast<CaptureChain*>(source)) {
        return TryGetCaptureDevice(chain->Current(), out);
    }

    // 区域源内部复用整屏 DXGI 源，设备在内层。
    if (auto* region = dynamic_cast<RegionCaptureSource*>(source)) {
        return TryGetCaptureDevice(region->Inner(), out);
    }

    if (auto* dxgi = dynamic_cast<DxgiCaptureSource*>(source)) {
        out.device = dxgi->Device();
        out.context = dxgi->Context();
        return out.Valid();
    }

    if (auto* wgc = dynamic_cast<WgcCaptureSource*>(source)) {
        out.device = wgc->Device();
        out.context = wgc->Context();
        return out.Valid();
    }

    return false;
}

}  // namespace sr::capture

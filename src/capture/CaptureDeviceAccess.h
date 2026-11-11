#pragma once

#include "core/interfaces/ICaptureSource.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace sr::capture {

struct CaptureDevice {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    bool Valid() const { return device != nullptr && context != nullptr; }
};

// 从任意采集实现（含降级链）里取出它正在用的 D3D11 设备。
// 纹理跨设备不可拷贝，所以合成/转换层必须拿到「产出该帧的那块设备」。
// 放在采集层而不是 core 接口里，是为了避免把 D3D11 泄漏到 M6-13 要求纯净的抽象层。
bool TryGetCaptureDevice(ICaptureSource* source, CaptureDevice& out);

}  // namespace sr::capture

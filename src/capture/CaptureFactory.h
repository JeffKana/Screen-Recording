#pragma once

#include <memory>
#include <string>

#include "core/interfaces/ICaptureSource.h"
#include "core/types/IClock.h"

namespace sr::capture {

// 采集实现的选择与降级入口。上层（recorder / UI）只调这一个工厂，
// 不感知具体后端，从而满足 M6-13 的「新增平台只改采集层」。
class CaptureFactory {
public:
    // 失败返回 nullptr，error 说明原因（可直接展示给用户，对应 M8-08 的错误码语义）。
    static std::unique_ptr<ICaptureSource> Create(const IClock& clock,
                                                  const CaptureRequest& request,
                                                  std::string& error);

    // 后端标识，用于诊断包与日志（M8-14）。
    static const char* BackendFor(CaptureKind kind);
};

}  // namespace sr::capture

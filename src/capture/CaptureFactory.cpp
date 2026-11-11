#include "capture/CaptureFactory.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "capture/CaptureChain.h"
#include "capture/DxgiCaptureSource.h"
#include "capture/RegionCaptureSource.h"
#include "capture/WgcCaptureSource.h"
#include "capture/WindowCaptureSource.h"

namespace sr::capture {
namespace {

bool Equals(const char* text, const char* expected) {
    return text != nullptr && std::strcmp(text, expected) == 0;
}

}  // namespace

const char* CaptureFactory::BackendFor(CaptureKind kind) {
    switch (kind) {
        case CaptureKind::Display:
            return "wgc|dxgi-dd";
        case CaptureKind::Region:
            return "dxgi-dd-region";
        case CaptureKind::Window:
            return "wgc|printwindow";
    }
    return "unknown";
}

std::unique_ptr<ICaptureSource> CaptureFactory::Create(const IClock& clock,
                                                       const CaptureRequest& request,
                                                       std::string& error) {
    // 区域采集内部复用整屏源，保持独立实现。
    if (request.kind == CaptureKind::Region) {
        error.clear();
        return std::make_unique<RegionCaptureSource>(clock);
    }

    std::vector<CaptureChain::BackendFactory> factories;
    std::vector<std::string> names;

    auto add_wgc = [&]() {
        factories.push_back([&clock]() { return std::make_unique<WgcCaptureSource>(clock); });
        names.emplace_back("wgc");
    };
    auto add_dxgi = [&]() {
        factories.push_back([&clock]() { return std::make_unique<DxgiCaptureSource>(clock); });
        names.emplace_back("dxgi");
    };
    auto add_printwindow = [&]() {
        factories.push_back([&clock]() { return std::make_unique<WindowCaptureSource>(clock); });
        names.emplace_back("printwindow");
    };

    const char* preferred = request.preferred_backend;
    if (Equals(preferred, "wgc")) {
        add_wgc();
    } else if (Equals(preferred, "dxgi")) {
        add_dxgi();
    } else if (Equals(preferred, "printwindow")) {
        add_printwindow();
    } else if (request.kind == CaptureKind::Window) {
        add_wgc();
        add_printwindow();
    } else {
        add_wgc();
        add_dxgi();
    }

    if (factories.empty()) {
        error = "no backend matches request";
        return nullptr;
    }

    error.clear();
    return std::make_unique<CaptureChain>(std::move(factories), std::move(names));
}

}  // namespace sr::capture

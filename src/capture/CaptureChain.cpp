#include "capture/CaptureChain.h"

#include <utility>

namespace sr::capture {

CaptureChain::CaptureChain(std::vector<BackendFactory> backends, std::vector<std::string> names)
    : backends_(std::move(backends)), names_(std::move(names)) {}

bool CaptureChain::ActivateFrom(size_t index, std::string& error) {
    for (size_t i = index; i < backends_.size(); ++i) {
        if (!excluded_.empty() && names_[i] == excluded_) {
            trace_ += names_[i] + ":excluded; ";
            continue;
        }
        std::unique_ptr<ICaptureSource> candidate = backends_[i]();
        if (!candidate) {
            trace_ += names_[i] + ":create_null; ";
            continue;
        }
        std::string start_error;
        if (candidate->Start(request_, start_error)) {
            current_ = std::move(candidate);
            active_ = i;
            active_name_ = names_[i];
            trace_ += names_[i] + ":ok; ";
            running_ = true;
            error.clear();
            return true;
        }
        trace_ += names_[i] + ":fail(" + start_error + "); ";
        error = start_error;
    }
    current_.reset();
    running_ = false;
    active_name_ = "none";
    if (error.empty()) {
        error = "no capture backend available";
    }
    return false;
}

bool CaptureChain::Start(const CaptureRequest& request, std::string& error) {
    Stop();
    request_ = request;
    excluded_ = request.excluded_backend != nullptr ? request.excluded_backend : "";
    trace_.clear();
    return ActivateFrom(0, error);
}

void CaptureChain::Stop() {
    if (current_) {
        current_->Stop();
        current_.reset();
    }
    running_ = false;
}

bool CaptureChain::IsRunning() const {
    return running_ && current_ && current_->IsRunning();
}

bool CaptureChain::TryAcquire(VideoFrame& out) {
    if (!running_ || !current_) {
        return false;
    }

    if (current_->NeedsRebuild()) {
        // 先原地重建；仍失败则降级到下一个后端。
        std::string rebuild_error;
        current_->Stop();
        if (!current_->Start(request_, rebuild_error)) {
            trace_ += "rebuild_fail(" + rebuild_error + "); ";
            if (!ActivateFrom(active_ + 1, rebuild_error)) {
                return false;
            }
        } else {
            trace_ += "rebuilt; ";
        }
    }

    if (current_ && current_->TryAcquire(out)) {
        return true;
    }
    return false;
}

SourceCaps CaptureChain::Caps() const {
    return current_ ? current_->Caps() : SourceCaps{};
}

bool CaptureChain::NeedsRebuild() const {
    return running_ && current_ && current_->NeedsRebuild();
}

}  // namespace sr::capture

#pragma once

#include <cstddef>

namespace sr::win {

// 极简 COM 智能指针。MinGW 没有 WRL（wrl/client.h），而契约要求零三方依赖，
// 所以自带一个够用即可的实现：AddRef/Release + 移动语义，不做 QueryInterface 封装。
//
// 放在 platform 层而不是某个业务目录：capture 与 audio 都要用，而 CONTRACT §3
// 禁止业务层互相依赖，公共设施只能下沉到 platform。
template <typename T>
class ComPtr {
public:
    ComPtr() = default;

    ~ComPtr() { Reset(); }

    ComPtr(const ComPtr& other) : ptr_(other.ptr_) {
        if (ptr_ != nullptr) {
            ptr_->AddRef();
        }
    }

    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }

    ComPtr& operator=(const ComPtr& other) {
        if (other.ptr_ != ptr_) {
            if (other.ptr_ != nullptr) {
                other.ptr_->AddRef();
            }
            Reset();
            ptr_ = other.ptr_;
        }
        return *this;
    }

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* Get() const { return ptr_; }
    T* operator->() const { return ptr_; }

    // 取出用于「传出参数」的地址。会先释放已有引用，避免重复创建时泄漏。
    T** GetAddressOf() {
        Reset();
        return &ptr_;
    }

    // 持有外部指针的引用（不接管所有权，会 AddRef）。
    void Attach(T* ptr) {
        Reset();
        if (ptr != nullptr) {
            ptr->AddRef();
            ptr_ = ptr;
        }
    }

    void Reset() {
        if (ptr_ != nullptr) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

    explicit operator bool() const { return ptr_ != nullptr; }

    // 允许 `ptr == nullptr` 这类写法，避免各处写 `!ptr` 或 `ptr.Get() != nullptr`。
    bool operator==(std::nullptr_t) const noexcept { return ptr_ == nullptr; }
    bool operator!=(std::nullptr_t) const noexcept { return ptr_ != nullptr; }

private:
    T* ptr_ = nullptr;
};

}  // namespace sr::win

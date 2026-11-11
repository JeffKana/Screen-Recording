// srprobe —— G2 采集与音频链路的实测探针。
// 用途：在没有编码/封装链路（G3）之前，用可量化指标验证采集与音频时间轴是否正确。
// 输出为 key=value 行，便于验收脚本 grep。

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "audio/AudioTimeline.h"
#include "audio/FloatMixer.h"
#include "audio/WasapiSource.h"
#include "capture/CaptureChain.h"
#include "capture/CaptureFactory.h"
#include "capture/DxgiCaptureSource.h"
#include "capture/WindowCaptureSource.h"
#include "platform/win/QpcClock.h"

#include <d3d11.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// 轮 38：自建目标窗口 + 遮挡窗口（M1-08/M1-09 验收支撑）。
int RunWindowHostCommand(int argc, char** argv);

namespace {

volatile bool g_stimulus_running = false;
HWND g_stimulus_window = nullptr;

LRESULT CALLBACK StimulusProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

// 静止桌面上 DXGI Desktop Duplication 不会产出任何帧（这是设计，不是缺陷）。
// 为了测量真实吞吐，需要人为制造桌面变化：这里用一个来回移动的小窗口。
DWORD WINAPI StimulusThread(LPVOID) {
    int x = 80;
    int y = 80;
    int dx = 9;
    int dy = 7;
    while (g_stimulus_running) {
        x += dx;
        y += dy;
        if (x < 0 || x > 700) {
            dx = -dx;
        }
        if (y < 0 || y > 500) {
            dy = -dy;
        }
        SetWindowPos(g_stimulus_window, nullptr, x, y, 180, 180, SWP_NOZORDER | SWP_NOACTIVATE);
        Sleep(8);
    }
    return 0;
}

bool StartStimulus(std::string& error) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = StimulusProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SrProbeStimulus";
    wc.hbrBackground = CreateSolidBrush(RGB(30, 144, 255));
    RegisterClassW(&wc);

    g_stimulus_window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                        L"SrProbeStimulus",
                                        L"probe",
                                        WS_POPUP | WS_VISIBLE,
                                        80,
                                        80,
                                        180,
                                        180,
                                        nullptr,
                                        nullptr,
                                        GetModuleHandleW(nullptr),
                                        nullptr);
    if (g_stimulus_window == nullptr) {
        error = "CreateWindowEx failed";
        return false;
    }
    g_stimulus_running = true;
    HANDLE thread = CreateThread(nullptr, 0, StimulusThread, nullptr, 0, nullptr);
    if (thread == nullptr) {
        error = "CreateThread failed";
        return false;
    }
    CloseHandle(thread);
    return true;
}

void StopStimulus() {
    g_stimulus_running = false;
    if (g_stimulus_window != nullptr) {
        DestroyWindow(g_stimulus_window);
        g_stimulus_window = nullptr;
    }
    UnregisterClassW(L"SrProbeStimulus", GetModuleHandleW(nullptr));
}

void PrintUsage() {
    std::printf(
        "srprobe <command> [options]\n"
        "  outputs\n"
        "  capture --source display|region|window [--display N] [--region x,y,w,h]\n"
        "          [--hwnd 0xNNNN] [--seconds N] [--fps N] [--stimulate]\n"
        "  audio   [--seconds N] [--no-mic] [--gain-sys X] [--gain-mic X]\n");
}

struct CaptureOptions {
    std::string source = "display";
    uint32_t display = 0;
    int32_t rx = 0;
    int32_t ry = 0;
    int32_t rw = 0;
    int32_t rh = 0;
    uint64_t hwnd = 0;
    uint32_t seconds = 10;
    uint32_t fps = 30;
    bool stimulate = false;
    std::string backend;  // 空 = 自动降级链
};

struct AudioOptions {
    uint32_t seconds = 10;
    bool use_mic = true;
    float gain_sys = 1.0f;
    float gain_mic = 1.0f;
};

int RunOutputs() {
    std::string description;
    const uint32_t count = sr::capture::DxgiCaptureSource::EnumerateOutputs(description);
    std::printf("outputs=%u\n", count);
    std::printf("detail<<\n%s>>\n", description.c_str());
    return count > 0 ? 0 : 1;
}

int RunCapture(const CaptureOptions& options) {
    sr::win::QpcClock clock;

    sr::CaptureRequest request;
    request.include_cursor = true;
    request.target_fps = options.fps;
    request.preferred_backend = options.backend.empty() ? nullptr : options.backend.c_str();
    request.display_index = options.display;
    if (options.source == "region") {
        request.kind = sr::CaptureKind::Region;
        request.region.x = options.rx;
        request.region.y = options.ry;
        request.region.width = options.rw;
        request.region.height = options.rh;
    } else if (options.source == "window") {
        request.kind = sr::CaptureKind::Window;
        request.window_handle = options.hwnd != 0
                                    ? options.hwnd
                                    : reinterpret_cast<uint64_t>(GetForegroundWindow());
    } else {
        request.kind = sr::CaptureKind::Display;
    }

    std::string error;
    std::unique_ptr<sr::ICaptureSource> source = sr::capture::CaptureFactory::Create(clock, request, error);
    if (!source) {
        std::printf("create=FAIL error=%s\n", error.c_str());
        return 2;
    }
    if (!source->Start(request, error)) {
        std::printf("start=FAIL error=%s backend=%s\n", error.c_str(), sr::capture::CaptureFactory::BackendFor(request.kind));
        return 3;
    }

    if (options.stimulate) {
        if (!StartStimulus(error)) {
            std::printf("stimulate=FAIL error=%s\n", error.c_str());
        }
    }

    const sr::SourceCaps caps = source->Caps();
    std::printf("backend=%s\n", caps.backend);
    std::printf("size=%ux%u\n", caps.max_size.width, caps.max_size.height);
    std::printf("occlusion_supported=%d\n", caps.supports_occlusion ? 1 : 0);
    if (auto* chain = dynamic_cast<sr::capture::CaptureChain*>(source.get())) {
        std::printf("active_backend=%s\n", chain->ActiveBackend().c_str());
        std::printf("chain_trace=%s\n", chain->Trace().c_str());
    }

    std::vector<int64_t> intervals;
    intervals.reserve(4096);
    uint64_t frames = 0;
    uint64_t waits = 0;
    uint64_t rebuilds = 0;
    sr::Tick previous{};
    const sr::Tick begin = clock.Now();

    for (;;) {
        const int64_t elapsed = clock.ElapsedMs(begin, clock.Now());
        if (elapsed >= static_cast<int64_t>(options.seconds) * 1000) {
            break;
        }
        sr::VideoFrame frame;
        if (source->TryAcquire(frame)) {
            if (previous.Valid()) {
                intervals.push_back(clock.ElapsedMs(previous, frame.desc.pts));
            }
            previous = frame.desc.pts;
            ++frames;
        } else {
            ++waits;
            if (source->NeedsRebuild()) {
                ++rebuilds;
                std::string rebuild_error;
                source->Stop();
                if (source->Start(request, rebuild_error)) {
                    previous = sr::Tick{};
                } else {
                    std::printf("rebuild=FAIL error=%s\n", rebuild_error.c_str());
                    break;
                }
            }
            Sleep(1);
        }
    }

    if (options.stimulate) {
        StopStimulus();
    }

    const int64_t wall_ms = clock.ElapsedMs(begin, clock.Now());
    source->Stop();

    int64_t sum = 0;
    int64_t max_interval = 0;
    for (int64_t value : intervals) {
        sum += value;
        if (value > max_interval) {
            max_interval = value;
        }
    }
    const double avg = intervals.empty() ? 0.0 : static_cast<double>(sum) / static_cast<double>(intervals.size());
    int64_t p99 = 0;
    if (!intervals.empty()) {
        std::vector<int64_t> sorted = intervals;
        std::sort(sorted.begin(), sorted.end());
        p99 = sorted[static_cast<size_t>(static_cast<double>(sorted.size() - 1) * 0.99)];
    }

    std::printf("seconds=%.2f\n", static_cast<double>(wall_ms) / 1000.0);
    std::printf("frames=%llu\n", static_cast<unsigned long long>(frames));
    std::printf("waits=%llu\n", static_cast<unsigned long long>(waits));
    std::printf("rebuilds=%llu\n", static_cast<unsigned long long>(rebuilds));
    std::printf("fps_actual=%.2f\n", wall_ms > 0 ? static_cast<double>(frames) * 1000.0 / static_cast<double>(wall_ms) : 0.0);
    std::printf("interval_avg_ms=%.2f\n", avg);
    std::printf("interval_p99_ms=%lld\n", static_cast<long long>(p99));
    std::printf("interval_max_ms=%lld\n", static_cast<long long>(max_interval));

    uint64_t jitter = 0;
    if (avg > 0.0) {
        for (int64_t value : intervals) {
            if (static_cast<double>(value) > avg * 2.0) {
                ++jitter;
            }
        }
    }
    std::printf("frames_over_2x_avg=%llu\n", static_cast<unsigned long long>(jitter));

    // 环境可测性诊断：锁屏 / 显示器休眠 / RDP 断开时 DWM 会把桌面更新节流到约 1Hz，
    // 此时采集吞吐不具备测量意义（不是产品缺陷）。据此把这类运行判为 SKIP 而非 FAIL。
    DWORD session_id = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session_id);
    HDESK input_desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    std::printf("session_id=%lu\n", static_cast<unsigned long>(session_id));
    std::printf("input_desktop=%s\n", input_desktop != nullptr ? "ok" : "denied");
    if (input_desktop != nullptr) {
        CloseDesktop(input_desktop);
    }
    const bool throttled = avg > 200.0 && frames >= 3;
    std::printf("dwm_throttled=%d\n", throttled ? 1 : 0);

    return frames > 0 ? 0 : 4;
}

HWND g_self_window = nullptr;

LRESULT CALLBACK SelfTestWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH brush = CreateSolidBrush(RGB(0, 200, 0));
        FillRect(dc, &rc, brush);
        DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, L"SRPROBE", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

// 窗口采集自检（M1-07）：自建一个 GDI 窗口 → PrintWindow 采集 → 回读像素，
// 断言「画面不是全黑」。比依赖外部程序（记事本在 Win11 是打包应用）可靠得多。
int RunWindowSelfTest() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = SelfTestWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SrProbeSelfWindow";
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);

    g_self_window = CreateWindowExW(0,
                                    L"SrProbeSelfWindow",
                                    L"srprobe self test",
                                    WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                    200,
                                    200,
                                    420,
                                    320,
                                    nullptr,
                                    nullptr,
                                    GetModuleHandleW(nullptr),
                                    nullptr);
    if (g_self_window == nullptr) {
        std::printf("windowselftest=FAIL create_window\n");
        return 2;
    }

    // 让窗口完成首帧绘制。
    const DWORD paint_deadline = GetTickCount() + 500;
    while (GetTickCount() < paint_deadline) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }

    sr::win::QpcClock clock;
    sr::CaptureRequest request;
    request.kind = sr::CaptureKind::Window;
    request.window_handle = reinterpret_cast<uint64_t>(g_self_window);
    request.target_fps = 30;

    sr::capture::WindowCaptureSource source(clock);
    std::string error;
    if (!source.Start(request, error)) {
        std::printf("windowselftest=FAIL start error=%s\n", error.c_str());
        DestroyWindow(g_self_window);
        return 3;
    }

    sr::VideoFrame frame;
    uint64_t attempts = 0;
    bool acquired = false;
    const DWORD acquire_deadline = GetTickCount() + 2000;
    while (GetTickCount() < acquire_deadline) {
        ++attempts;
        if (source.TryAcquire(frame)) {
            acquired = true;
            break;
        }
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(20);
    }

    if (!acquired) {
        std::printf("windowselftest=FAIL no_frame attempts=%llu\n", static_cast<unsigned long long>(attempts));
        source.Stop();
        DestroyWindow(g_self_window);
        return 4;
    }

    // 回读像素统计（唯一允许 CPU 回读的场合：自检取证）。
    auto* texture = static_cast<ID3D11Texture2D*>(frame.native_texture);
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);

    ID3D11Device* device = nullptr;
    texture->GetDevice(&device);
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);

    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    HRESULT hr = device->CreateTexture2D(&staging_desc, nullptr, &staging);
    if (FAILED(hr)) {
        std::printf("windowselftest=FAIL staging_texture\n");
        context->Release();
        device->Release();
        source.Stop();
        DestroyWindow(g_self_window);
        return 5;
    }

    context->CopyResource(staging, texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        std::printf("windowselftest=FAIL map\n");
        staging->Release();
        context->Release();
        device->Release();
        source.Stop();
        DestroyWindow(g_self_window);
        return 6;
    }

    uint64_t non_black = 0;
    uint64_t green = 0;
    uint64_t total = 0;
    for (UINT row = 0; row < staging_desc.Height; row += 4) {
        const auto* pixels = reinterpret_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch;
        for (UINT col = 0; col < staging_desc.Width; col += 4) {
            const uint8_t blue = pixels[col * 4 + 0];
            const uint8_t green_channel = pixels[col * 4 + 1];
            const uint8_t red = pixels[col * 4 + 2];
            ++total;
            if (blue != 0 || green_channel != 0 || red != 0) {
                ++non_black;
            }
            if (green_channel > 150 && red < 80 && blue < 80) {
                ++green;
            }
        }
    }
    context->Unmap(staging, 0);

    const double ratio = total == 0 ? 0.0 : static_cast<double>(non_black) / static_cast<double>(total);
    const double green_ratio = total == 0 ? 0.0 : static_cast<double>(green) / static_cast<double>(total);

    std::printf("windowselftest=frames=1 attempts=%llu\n", static_cast<unsigned long long>(attempts));
    std::printf("window_size=%ux%u\n", staging_desc.Width, staging_desc.Height);
    std::printf("non_black_ratio=%.4f\n", ratio);
    std::printf("green_ratio=%.4f\n", green_ratio);

    staging->Release();
    context->Release();
    device->Release();
    source.Stop();
    DestroyWindow(g_self_window);
    UnregisterClassW(L"SrProbeSelfWindow", GetModuleHandleW(nullptr));

    return (ratio > 0.5 && green_ratio > 0.3) ? 0 : 7;
}

// 混音器自检：用合成信号验证 M1-20（独立增益/静音）与限幅行为。
// 不依赖任何音频设备，可在无人值守环境跑。
int RunMixSelfTest() {
    const uint32_t frames = 480;
    const uint16_t channels = 2;
    // 取 0.35 / 0.25 而不是 0.3 / 0.2：后者在 2 倍增益下正好等于 1.0，
    // 不会触发 `> 1.0` 的限幅判定，测不出削波行为。
    std::vector<float> sys_data(static_cast<size_t>(frames) * channels, 0.35f);
    std::vector<float> mic_data(static_cast<size_t>(frames) * channels, 0.25f);
    std::vector<float> out(static_cast<size_t>(frames) * channels, 0.0f);

    sr::AudioBlock sys_block;
    sys_block.samples = sys_data.data();
    sys_block.desc.first_sample = 0;
    sys_block.desc.sample_rate = 48000;
    sys_block.desc.channels = channels;
    sys_block.desc.frame_count = frames;

    sr::AudioBlock mic_block;
    mic_block.samples = mic_data.data();
    mic_block.desc = sys_block.desc;

    const sr::AudioBlock* inputs[2] = {&sys_block, &mic_block};

    sr::audio::FloatMixer mixer;
    mixer.SetInputCount(2);
    int failures = 0;

    auto check = [&failures](const char* name, bool ok, float actual, float expected) {
        std::printf("%s %s actual=%.4f expected=%.4f\n", ok ? "PASS" : "FAIL", name, actual, expected);
        if (!ok) {
            ++failures;
        }
    };

    mixer.SetGain(0, 1.0f);
    mixer.SetGain(1, 1.0f);
    uint32_t produced = mixer.Mix(inputs, 2, out.data(), static_cast<uint32_t>(out.size()));
    check("mix_sum", produced == frames * channels && std::fabs(out[0] - 0.6f) < 1e-4f, out[0], 0.6f);
    check("no_clip", !mixer.LastBlockClipped(), 0.0f, 0.0f);

    mixer.SetGain(0, 2.0f);
    mixer.SetGain(1, 2.0f);
    mixer.Mix(inputs, 2, out.data(), static_cast<uint32_t>(out.size()));
    check("clip_to_one", std::fabs(out[0] - 1.0f) < 1e-4f && mixer.LastBlockClipped(), out[0], 1.0f);

    mixer.SetGain(0, 1.0f);
    mixer.SetGain(1, 1.0f);
    mixer.SetMuted(1, true);
    mixer.Mix(inputs, 2, out.data(), static_cast<uint32_t>(out.size()));
    check("mute_mic", std::fabs(out[0] - 0.35f) < 1e-4f, out[0], 0.35f);

    mixer.SetMuted(1, false);
    mixer.SetMuted(0, true);
    mixer.Mix(inputs, 2, out.data(), static_cast<uint32_t>(out.size()));
    check("mute_system", std::fabs(out[0] - 0.25f) < 1e-4f, out[0], 0.25f);

    std::printf("failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}

std::string DisplayName(const wchar_t* name) {
    // 设备友好名通常是中文（如「扬声器 (Realtek)」）。窄 printf 的 %ls 在
    // 当前控制台代码页下可能转换失败，连后面的 \n 一起吞掉，导致下一行
    // loopback_rate= 粘在同一行，g2 的正则读不到值。
    // 探针只需要稳定的诊断输出：这里只保留 ASCII 可打印字符。
    if (name == nullptr || name[0] == L'\0') {
        return "<unnamed>";
    }
    std::string clean;
    for (const wchar_t* p = name; *p != L'\0'; ++p) {
        if (*p >= 0x20 && *p < 0x7F) {
            clean.push_back(static_cast<char>(*p));
        }
    }
    return clean.empty() ? "<unnamed>" : clean;
}

int RunAudio(const AudioOptions& options) {
    sr::win::QpcClock clock;

    auto loopback = std::make_unique<sr::audio::WasapiSource>(clock, sr::audio::AudioRole::SystemLoopback);
    std::string error;
    if (!loopback->Start(error)) {
        std::printf("loopback=FAIL error=%s\n", error.c_str());
        return 2;
    }
    std::printf("loopback_device=%s\n", DisplayName(loopback->DeviceName()).c_str());
    std::printf("loopback_rate=%u\n", loopback->SampleRate());
    std::printf("loopback_channels=%u\n", loopback->Channels());

    std::unique_ptr<sr::audio::WasapiSource> mic;
    if (options.use_mic) {
        mic = std::make_unique<sr::audio::WasapiSource>(clock, sr::audio::AudioRole::Microphone);
        std::string mic_error;
        if (mic->Start(mic_error)) {
            std::printf("mic_device=%s\n", DisplayName(mic->DeviceName()).c_str());
        } else {
            std::printf("mic_device=none error=%s\n", mic_error.c_str());
            mic.reset();
        }
    }

    const uint32_t rate = loopback->SampleRate();
    const uint16_t channels = loopback->Channels();

    sr::audio::AudioTimeline loopback_timeline(clock, rate, channels);
    sr::audio::AudioTimeline mic_timeline(clock, rate, channels);
    sr::audio::FloatMixer mixer;
    mixer.SetInputCount(mic ? 2 : 1);
    mixer.SetGain(0, options.gain_sys);
    mixer.SetGain(1, options.gain_mic);

    constexpr uint32_t kBlockFrames = 960;  // 20ms @48k
    // 输入与输出必须是三块独立缓冲：混音器会先清空 out，若 out 与某一路输入同位，
    // 那一路的数据会被提前抹掉（D-06）。
    std::vector<float> sys_buffer(static_cast<size_t>(kBlockFrames) * channels, 0.0f);
    std::vector<float> mic_buffer(static_cast<size_t>(kBlockFrames) * channels, 0.0f);
    std::vector<float> mix_buffer(static_cast<size_t>(kBlockFrames) * channels, 0.0f);

    uint64_t mixed_frames = 0;
    uint64_t clipped_blocks = 0;
    float peak = 0.0f;
    float sys_peak_max = 0.0f;
    float mic_peak_max = 0.0f;
    const sr::Tick begin = clock.Now();

    for (;;) {
        const int64_t elapsed = clock.ElapsedMs(begin, clock.Now());
        if (elapsed >= static_cast<int64_t>(options.seconds) * 1000) {
            break;
        }

        const uint32_t sys_frames = loopback_timeline.Pull(*loopback, sys_buffer.data(), kBlockFrames);
        uint32_t frames = sys_frames;

        if (mic) {
            const uint32_t mic_frames = mic_timeline.Pull(*mic, mic_buffer.data(), kBlockFrames);
            if (mic_frames < frames) {
                frames = mic_frames;
            }
        }

        if (frames == 0) {
            Sleep(1);
            continue;
        }

        sr::AudioBlock sys_block;
        sys_block.samples = sys_buffer.data();
        sys_block.desc.first_sample = static_cast<int64_t>(mixed_frames);
        sys_block.desc.sample_rate = rate;
        sys_block.desc.channels = channels;
        sys_block.desc.frame_count = frames;

        sr::AudioBlock mic_block;
        mic_block.samples = mic_buffer.data();
        mic_block.desc = sys_block.desc;

        const sr::AudioBlock* inputs[2] = {&sys_block, &mic_block};
        const uint32_t produced = mixer.Mix(inputs, mic ? 2u : 1u, mix_buffer.data(),
                                            static_cast<uint32_t>(mix_buffer.size()));
        if (mixer.LastBlockClipped()) {
            ++clipped_blocks;
        }
        mixed_frames += produced / channels;

        for (uint32_t i = 0; i < produced; ++i) {
            const float magnitude = mix_buffer[i] < 0 ? -mix_buffer[i] : mix_buffer[i];
            if (magnitude > peak) {
                peak = magnitude;
            }
        }

        // 逐路统计全程峰值。注意 WasapiSource::PeakLevel() 只是「最近一块」的峰值，
        // 用它描述整段录音会严重低估（首次实现就踩过这个坑，见台账 D-07）。
        for (uint32_t i = 0; i < frames * channels; ++i) {
            const float sys_value = sys_buffer[i] < 0 ? -sys_buffer[i] : sys_buffer[i];
            if (sys_value > sys_peak_max) {
                sys_peak_max = sys_value;
            }
            if (mic) {
                const float mic_value = mic_buffer[i] < 0 ? -mic_buffer[i] : mic_buffer[i];
                if (mic_value > mic_peak_max) {
                    mic_peak_max = mic_value;
                }
            }
        }
    }

    loopback->Stop();
    if (mic) {
        mic->Stop();
    }

    const uint64_t expected = loopback_timeline.ExpectedFrames();
    const uint64_t produced = loopback_timeline.ProducedFrames();
    std::printf("seconds=%.2f\n", static_cast<double>(clock.ElapsedMs(begin, clock.Now())) / 1000.0);
    std::printf("expected_frames=%llu\n", static_cast<unsigned long long>(expected));
    std::printf("produced_frames=%llu\n", static_cast<unsigned long long>(produced));
    std::printf("silence_filled=%llu\n", static_cast<unsigned long long>(loopback_timeline.SilenceFilledFrames()));
    std::printf("source_gaps=%llu\n", static_cast<unsigned long long>(loopback_timeline.SourceGapCount()));
    std::printf("mixed_frames=%llu\n", static_cast<unsigned long long>(mixed_frames));
    std::printf("clipped_blocks=%llu\n", static_cast<unsigned long long>(clipped_blocks));
    std::printf("peak=%.4f\n", peak);
    std::printf("loopback_peak_max=%.4f\n", sys_peak_max);
    std::printf("mic_used=%d\n", mic ? 1 : 0);
    if (mic) {
        std::printf("mic_peak_max=%.4f\n", mic_peak_max);
        std::printf("mic_produced_frames=%llu\n", static_cast<unsigned long long>(mic_timeline.ProducedFrames()));
    }

    const long long drift = static_cast<long long>(produced) - static_cast<long long>(expected);
    std::printf("duration_drift_frames=%lld\n", drift);
    return (expected > 0 && drift == 0) ? 0 : 5;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const std::string command = argv[1];
    if (command == "outputs") {
        return RunOutputs();
    }

    if (command == "mixselftest") {
        return RunMixSelfTest();
    }

    if (command == "windowselftest") {
        return RunWindowSelfTest();
    }

    if (command == "windowhost") {
        return RunWindowHostCommand(argc, argv);
    }

    if (command == "capture") {
        CaptureOptions options;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--source" && i + 1 < argc) {
                options.source = argv[++i];
            } else if (arg == "--display" && i + 1 < argc) {
                options.display = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            } else if (arg == "--region" && i + 1 < argc) {
                std::sscanf(argv[++i], "%d,%d,%d,%d", &options.rx, &options.ry, &options.rw, &options.rh);
            } else if (arg == "--hwnd" && i + 1 < argc) {
                options.hwnd = std::strtoull(argv[++i], nullptr, 0);
            } else if (arg == "--seconds" && i + 1 < argc) {
                options.seconds = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            } else if (arg == "--fps" && i + 1 < argc) {
                options.fps = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            } else if (arg == "--backend" && i + 1 < argc) {
                options.backend = argv[++i];
            } else if (arg == "--stimulate") {
                options.stimulate = true;
            } else {
                PrintUsage();
                return 1;
            }
        }
        return RunCapture(options);
    }

    if (command == "audio") {
        AudioOptions options;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--seconds" && i + 1 < argc) {
                options.seconds = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            } else if (arg == "--no-mic") {
                options.use_mic = false;
            } else if (arg == "--gain-sys" && i + 1 < argc) {
                options.gain_sys = static_cast<float>(std::atof(argv[++i]));
            } else if (arg == "--gain-mic" && i + 1 < argc) {
                options.gain_mic = static_cast<float>(std::atof(argv[++i]));
            } else {
                PrintUsage();
                return 1;
            }
        }
        return RunAudio(options);
    }

    PrintUsage();
    return 1;
}

#pragma once

#include <cstdint>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace sr::app {

// 全局热键组合。modifiers 使用 Win32 的 MOD_* 位；vk 为虚拟键码。
struct HotkeyCombo {
    UINT modifiers = 0;
    UINT vk = 0;

    bool empty() const { return vk == 0; }
    bool operator==(const HotkeyCombo& other) const {
        return modifiers == other.modifiers && vk == other.vk;
    }
};

// M1-04 / M1-05：区域录制。坐标/尺寸均为**物理像素**（per-monitor v2）。
// width/height 为 0 表示全屏。
struct CaptureRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool enabled() const { return width > 0 && height > 0; }
    bool operator==(const CaptureRegion& other) const {
        return x == other.x && y == other.y && width == other.width && height == other.height;
    }
};

struct AppConfig {
    HotkeyCombo start;
    HotkeyCombo pause;
    HotkeyCombo stop;
    std::wstring output_dir;
    std::wstring recorder_path;
    // M5-02：UI 侧展开的命名模板；支持 {date} {time} {source} {app} {index}。
    std::wstring filename_template = L"sr_{date}_{time}_{index}";
    // M2-09 / M8-04：剩余可录时长的输入；与 Recorder 的 --bitrate/--disk-stop-mb 保持一致。
    uint32_t video_bitrate_kbps = 6000;
    uint32_t disk_stop_mb = 128;
    // M8-03：磁盘水位的两级提示线（MB）。
    //   disk_warn_mb     —— 非阻塞提示线（需求：剩余 < 2GB 提示，录制继续）
    //   disk_critical_mb —— 强提示线（需求：剩余 < 500MB 升级为强提示）
    // 停止线 disk_stop_mb 仍是 Recorder 的硬停止线，与提示线是两回事。
    uint32_t disk_warn_mb = 2048;
    uint32_t disk_critical_mb = 500;
    // M1-04：区域录制（宽高为 0 = 全屏）。
    CaptureRegion region;
    // M3-03：帧率入口（15/24/25/30/60/自定义 1~144）。
    uint32_t video_fps = 30;
    // M3-04：码率模式 cbr / vbr（VBR = 平均码率 + 2× 峰值上限）。
    std::wstring rate_mode = L"cbr";
    // M3-10：画质预设档位（极致 / 高清 / 标准 / 省空间 / 自定义）。
    // 联动的是 fps / bitrate / quality 三项；分辨率不随档位缩放。
    std::wstring quality_preset = L"高清";
    uint32_t quality = 0;
    // 轮 30：日志分级（INI [log] level，可被 --log-level 覆盖）。
    std::wstring log_level = L"info";
};

// 偶数对齐 + 最小尺寸；UI 是唯一对齐点，Recorder 的偶数对齐只作为兜底。
CaptureRegion NormalizeRegion(const CaptureRegion& region);

// "Ctrl+Alt+R" 形式；解析失败时 error 说明原因。
std::wstring FormatHotkey(const HotkeyCombo& combo);
bool ParseHotkey(const std::wstring& text, HotkeyCombo& combo, std::wstring& error);

std::wstring DefaultConfigPath();
std::wstring DefaultLogPath();
std::wstring DefaultOutputDir();
std::wstring DefaultRecorderPath();

bool LoadConfig(const std::wstring& path, AppConfig& config, std::wstring& error);

// 写入 [hotkeys] <action> = <combo>，并回读校验。action: start/pause/stop。
bool SaveHotkey(const std::wstring& path,
                const std::wstring& action,
                const HotkeyCombo& combo,
                std::wstring& error);

// M5-01 / M5-02：保存输出目录与命名模板（写入后回读校验）。
bool SaveOutputDir(const std::wstring& path, const std::wstring& directory, std::wstring& error);
bool SaveTemplate(const std::wstring& path, const std::wstring& filename_template, std::wstring& error);
bool SaveRegion(const std::wstring& path, const CaptureRegion& region, std::wstring& error);

// M3-03 / M3-04：帧率与码率模式（写入 [video] 段，逐项回读校验）。
bool SaveVideoSettings(const std::wstring& path,
                       uint32_t fps,
                       const std::wstring& rate_mode,
                       std::wstring& error);

// M3-10：画质预设档位。四档联动 fps / bitrate_kbps / quality；
// 「自定义」不改动任何值，供高级面板单独覆盖。分辨率不随档位缩放（已知限制）。
struct QualityPresetValues {
    uint32_t fps = 30;
    uint32_t bitrate_kbps = 6000;
    uint32_t quality = 70;
};
const wchar_t* const* QualityPresetNames();
size_t QualityPresetCount();
bool ApplyQualityPreset(const std::wstring& name, AppConfig& config);
// 写入 [video] quality_preset / quality + 联动后的 fps，逐项回读校验。
bool SaveQualityPreset(const std::wstring& path,
                       const std::wstring& name,
                       uint32_t fps,
                       uint32_t bitrate_kbps,
                       uint32_t quality,
                       std::wstring& error);

}  // namespace sr::app

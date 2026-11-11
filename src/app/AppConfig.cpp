#include "app/AppConfig.h"

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <vector>

namespace sr::app {
namespace {

constexpr const wchar_t* kSectionHotkeys = L"hotkeys";
constexpr const wchar_t* kSectionOutput = L"output";
constexpr const wchar_t* kSectionRecorder = L"recorder";
constexpr const wchar_t* kSectionRecord = L"record";
constexpr const wchar_t* kSectionVideo = L"video";
constexpr const wchar_t* kSectionLog = L"log";

std::wstring Trim(const std::wstring& text) {
    size_t begin = 0;
    while (begin < text.size() && std::iswspace(text[begin])) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin && std::iswspace(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::wstring ToLower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return text;
}

std::vector<std::wstring> Split(const std::wstring& text, wchar_t separator) {
    std::vector<std::wstring> parts;
    std::wstring current;
    for (wchar_t c : text) {
        if (c == separator) {
            parts.push_back(Trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(Trim(current));
    return parts;
}

std::wstring EnvironmentValue(const wchar_t* name) {
    wchar_t buffer[1024] = {};
    const DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return {};
    }
    return std::wstring(buffer, length);
}

std::wstring ReadProfile(const std::wstring& path,
                         const wchar_t* section,
                         const wchar_t* key,
                         const wchar_t* fallback) {
    wchar_t buffer[1024] = {};
    GetPrivateProfileStringW(section, key, fallback, buffer,
                             static_cast<DWORD>(std::size(buffer)), path.c_str());
    return Trim(buffer);
}

bool EnsureParentDirectory(const std::wstring& path, std::wstring& error) {
    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) {
        return true;
    }
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        error = L"cannot create directory: " + parent.wstring();
        return false;
    }
    return true;
}

bool ParseKeyToken(const std::wstring& token, UINT& vk) {
    if (token.size() == 1) {
        wchar_t c = token[0];
        if (c >= L'a' && c <= L'z') {
            c = static_cast<wchar_t>(c - L'a' + L'A');
        }
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) {
            vk = static_cast<UINT>(c);
            return true;
        }
        return false;
    }
    const std::wstring lower = ToLower(token);
    if (lower.size() >= 2 && lower[0] == L'f') {
        int number = 0;
        for (size_t i = 1; i < lower.size(); ++i) {
            if (lower[i] < L'0' || lower[i] > L'9') {
                return false;
            }
            number = number * 10 + (lower[i] - L'0');
        }
        if (number >= 1 && number <= 24) {
            vk = static_cast<UINT>(VK_F1 + number - 1);
            return true;
        }
        return false;
    }
    if (lower == L"space") {
        vk = VK_SPACE;
        return true;
    }
    if (lower == L"tab") {
        vk = VK_TAB;
        return true;
    }
    if (lower == L"enter" || lower == L"return") {
        vk = VK_RETURN;
        return true;
    }
    if (lower == L"esc" || lower == L"escape") {
        vk = VK_ESCAPE;
        return true;
    }
    return false;
}

std::wstring KeyToken(UINT vk) {
    if (vk >= VK_F1 && vk <= VK_F24) {
        return L"F" + std::to_wstring(vk - VK_F1 + 1);
    }
    if ((vk >= L'A' && vk <= L'Z') || (vk >= L'0' && vk <= L'9')) {
        return std::wstring(1, static_cast<wchar_t>(vk));
    }
    switch (vk) {
        case VK_SPACE:
            return L"Space";
        case VK_TAB:
            return L"Tab";
        case VK_RETURN:
            return L"Enter";
        case VK_ESCAPE:
            return L"Esc";
        default:
            break;
    }
    return L"";
}

}  // namespace

std::wstring FormatHotkey(const HotkeyCombo& combo) {
    std::wstring text;
    auto append = [&text](const wchar_t* part) {
        if (!text.empty()) {
            text += L"+";
        }
        text += part;
    };
    if ((combo.modifiers & MOD_CONTROL) != 0) {
        append(L"Ctrl");
    }
    if ((combo.modifiers & MOD_ALT) != 0) {
        append(L"Alt");
    }
    if ((combo.modifiers & MOD_SHIFT) != 0) {
        append(L"Shift");
    }
    if ((combo.modifiers & MOD_WIN) != 0) {
        append(L"Win");
    }
    const std::wstring key = KeyToken(combo.vk);
    if (!key.empty()) {
        append(key.c_str());
    }
    return text;
}

bool ParseHotkey(const std::wstring& text, HotkeyCombo& combo, std::wstring& error) {
    combo = HotkeyCombo{};
    const std::vector<std::wstring> parts = Split(Trim(text), L'+');
    if (parts.empty() || parts[0].empty()) {
        error = L"热键为空";
        return false;
    }

    UINT modifiers = 0;
    UINT vk = 0;
    bool has_key = false;
    for (const std::wstring& part : parts) {
        const std::wstring lower = ToLower(part);
        if (lower == L"ctrl" || lower == L"control") {
            modifiers |= MOD_CONTROL;
        } else if (lower == L"alt") {
            modifiers |= MOD_ALT;
        } else if (lower == L"shift") {
            modifiers |= MOD_SHIFT;
        } else if (lower == L"win" || lower == L"meta") {
            modifiers |= MOD_WIN;
        } else if (!has_key) {
            if (!ParseKeyToken(part, vk)) {
                error = L"无法识别的按键: " + part;
                return false;
            }
            has_key = true;
        } else {
            error = L"只能有一个主按键: " + part;
            return false;
        }
    }

    if (!has_key) {
        error = L"缺少主按键";
        return false;
    }
    if (modifiers == 0) {
        error = L"至少需要一个修饰键（Ctrl/Alt/Shift/Win）";
        return false;
    }

    combo.modifiers = modifiers;
    combo.vk = vk;
    error.clear();
    return true;
}

std::wstring DefaultConfigPath() {
    const std::wstring local = EnvironmentValue(L"LOCALAPPDATA");
    const std::wstring base = local.empty() ? EnvironmentValue(L"USERPROFILE") : local;
    if (base.empty()) {
        return L"sr-ui.ini";
    }
    return base + L"\\sr-ui\\config.ini";
}

std::wstring DefaultLogPath() {
    const std::wstring local = EnvironmentValue(L"LOCALAPPDATA");
    const std::wstring base = local.empty() ? EnvironmentValue(L"USERPROFILE") : local;
    if (base.empty()) {
        return L"sr-ui.log";
    }
    return base + L"\\sr-ui\\sr-ui.log";
}

std::wstring DefaultOutputDir() {
    const std::wstring profile = EnvironmentValue(L"USERPROFILE");
    if (profile.empty()) {
        return L".";
    }
    return profile + L"\\Videos\\ScreenRecorder";
}

std::wstring DefaultRecorderPath() {
    wchar_t module[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    if (length == 0 || length >= std::size(module)) {
        return L"sr-recorder.exe";
    }
    std::filesystem::path path(module);
    path.replace_filename(L"sr-recorder.exe");
    return path.wstring();
}

CaptureRegion NormalizeRegion(const CaptureRegion& region) {
    CaptureRegion normalized;
    // 只对**尺寸**做偶数对齐：801×601 -> 800×600（M3-02）。
    // x/y 保持原值，方向键才能按 1px 移动；区域源的裁剪偏移允许奇数。
    normalized.x = region.x;
    normalized.y = region.y;
    normalized.width = region.width & ~1;
    normalized.height = region.height & ~1;
    if (normalized.width < 2) {
        normalized.width = 0;
    }
    if (normalized.height < 2) {
        normalized.height = 0;
    }
    return normalized;
}

bool LoadConfig(const std::wstring& path, AppConfig& config, std::wstring& error) {
    HotkeyCombo default_start{};
    HotkeyCombo default_pause{};
    HotkeyCombo default_stop{};
    std::wstring parse_error;
    // 默认组合避开常见软件占用（实测 Ctrl+Alt+S 在本机被其它程序注册）。
    ParseHotkey(L"Ctrl+Alt+F9", default_start, parse_error);
    ParseHotkey(L"Ctrl+Alt+F10", default_pause, parse_error);
    ParseHotkey(L"Ctrl+Alt+F11", default_stop, parse_error);

    config = AppConfig{};
    config.start = default_start;
    config.pause = default_pause;
    config.stop = default_stop;
    config.output_dir = DefaultOutputDir();
    config.recorder_path = DefaultRecorderPath();

    struct Entry {
        const wchar_t* key;
        HotkeyCombo* target;
        const HotkeyCombo& fallback;
    };
    const Entry entries[] = {
        {L"start", &config.start, default_start},
        {L"pause", &config.pause, default_pause},
        {L"stop", &config.stop, default_stop},
    };
    for (const Entry& entry : entries) {
        const std::wstring fallback = FormatHotkey(entry.fallback);
        const std::wstring value = ReadProfile(path, kSectionHotkeys, entry.key, fallback.c_str());
        HotkeyCombo parsed{};
        std::wstring item_error;
        if (!ParseHotkey(value, parsed, item_error)) {
            error = std::wstring(L"热键 ") + entry.key + L" 无效（" + value + L"）: " + item_error;
            return false;
        }
        *entry.target = parsed;
    }

    const std::wstring output = ReadProfile(path, kSectionOutput, L"dir", L"");
    if (!output.empty()) {
        config.output_dir = output;
    }
    const std::wstring filename_template = ReadProfile(path, kSectionOutput, L"template", L"");
    if (!filename_template.empty()) {
        config.filename_template = filename_template;
    }
    const std::wstring bitrate = ReadProfile(path, kSectionOutput, L"bitrate_kbps", L"");
    if (!bitrate.empty()) {
        const unsigned long value = std::wcstoul(bitrate.c_str(), nullptr, 10);
        if (value == 0) {
            error = L"bitrate_kbps 无效: " + bitrate;
            return false;
        }
        config.video_bitrate_kbps = static_cast<uint32_t>(value);
    }
    const std::wstring disk_stop = ReadProfile(path, kSectionOutput, L"disk_stop_mb", L"");
    if (!disk_stop.empty()) {
        const unsigned long value = std::wcstoul(disk_stop.c_str(), nullptr, 10);
        if (value == 0) {
            error = L"disk_stop_mb 无效: " + disk_stop;
            return false;
        }
        config.disk_stop_mb = static_cast<uint32_t>(value);
    }
    // M8-03：非阻塞提示线（默认 2GB）与强提示线（默认 500MB）。
    // 允许配置覆盖是为了让「强提示」这条判定线在别的部署口径下也能调，
    // 验收用例用极大值触发提示、用极小值验证不误报。
    const std::wstring disk_warn = ReadProfile(path, kSectionOutput, L"disk_warn_mb", L"");
    if (!disk_warn.empty()) {
        const unsigned long value = std::wcstoul(disk_warn.c_str(), nullptr, 10);
        if (value == 0) {
            error = L"disk_warn_mb 无效: " + disk_warn;
            return false;
        }
        config.disk_warn_mb = static_cast<uint32_t>(value);
    }
    const std::wstring disk_critical = ReadProfile(path, kSectionOutput, L"disk_critical_mb", L"");
    if (!disk_critical.empty()) {
        const unsigned long value = std::wcstoul(disk_critical.c_str(), nullptr, 10);
        if (value == 0) {
            error = L"disk_critical_mb 无效: " + disk_critical;
            return false;
        }
        config.disk_critical_mb = static_cast<uint32_t>(value);
    }
    // M3-03：帧率入口。0 或缺省 = 沿用默认 30；越界视为配置损坏，直接报错。
    const std::wstring fps_text = ReadProfile(path, kSectionVideo, L"fps", L"");
    if (!fps_text.empty()) {
        const unsigned long value = std::wcstoul(fps_text.c_str(), nullptr, 10);
        if (value < 1 || value > 144) {
            error = L"[video] fps 超出 1~144: " + fps_text;
            return false;
        }
        config.video_fps = static_cast<uint32_t>(value);
    }
    // M3-04：码率模式。非法值不静默回落——静默回落会让「选了 VBR 却录成 CBR」看不见。
    const std::wstring rate_mode = ToLower(Trim(ReadProfile(path, kSectionVideo, L"rate_mode", L"")));
    if (!rate_mode.empty()) {
        if (rate_mode != L"cbr" && rate_mode != L"vbr") {
            error = L"[video] rate_mode 无效: " + rate_mode + L"（cbr/vbr）";
            return false;
        }
        config.rate_mode = rate_mode;
    }
    // M3-10：画质预设档位。缺省 = 高清（默认）；未知值不静默回落，写进 error 便于排查。
    const std::wstring preset = Trim(ReadProfile(path, kSectionVideo, L"quality_preset", L""));
    if (!preset.empty()) {
        config.quality_preset = preset;
    }
    const std::wstring quality_text = ReadProfile(path, kSectionVideo, L"quality", L"");
    if (!quality_text.empty()) {
        const unsigned long value = std::wcstoul(quality_text.c_str(), nullptr, 10);
        if (value > 100) {
            error = L"[video] quality 超出 0~100: " + quality_text;
            return false;
        }
        config.quality = static_cast<uint32_t>(value);
    }
    const std::wstring recorder = ReadProfile(path, kSectionRecorder, L"path", L"");
    if (!recorder.empty()) {
        config.recorder_path = recorder;
    }
    // 轮 30：日志分级。非法值不阻断启动，回落 info（启动期会记一行 warn）。
    const std::wstring log_level = ToLower(Trim(ReadProfile(path, kSectionLog, L"level", L"")));
    if (!log_level.empty()) {
        if (log_level == L"debug" || log_level == L"info" || log_level == L"warn") {
            config.log_level = log_level;
        } else {
            error = L"[log] level 无效: " + log_level + L"（debug/info/warn）";
            config.log_level = L"info";
            return false;
        }
    }
    const std::wstring region_text = ReadProfile(path, kSectionRecord, L"region", L"");
    if (!region_text.empty() && ToLower(region_text) != L"full") {
        const std::vector<std::wstring> parts = Split(region_text, L',');
        if (parts.size() != 4) {
            error = L"region 格式应为 x,y,w,h 或 full: " + region_text;
            return false;
        }
        long values[4] = {};
        for (size_t i = 0; i < parts.size(); ++i) {
            wchar_t* end = nullptr;
            values[i] = std::wcstol(parts[i].c_str(), &end, 10);
            if (parts[i].empty() || end == nullptr || *end != L'\0') {
                error = L"region 含非法整数: " + region_text;
                return false;
            }
        }
        CaptureRegion region{static_cast<int>(values[0]), static_cast<int>(values[1]),
                             static_cast<int>(values[2]), static_cast<int>(values[3])};
        region = NormalizeRegion(region);
        if (!region.enabled()) {
            error = L"region 尺寸无效: " + region_text;
            return false;
        }
        config.region = region;
    }
    error.clear();
    return true;
}

namespace {

bool WriteProfileVerified(const std::wstring& path,
                          const wchar_t* section,
                          const wchar_t* key,
                          const std::wstring& value,
                          std::wstring& error) {
    if (!EnsureParentDirectory(path, error)) {
        return false;
    }
    if (!WritePrivateProfileStringW(section, key, value.c_str(), path.c_str())) {
        error = L"写入 INI 失败, GetLastError=" + std::to_wstring(GetLastError());
        return false;
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    const std::wstring read_back = ReadProfile(path, section, key, L"");
    if (read_back != value) {
        error = L"INI 回读不一致: 期望 " + value + L"，实际 " + read_back;
        return false;
    }
    error.clear();
    return true;
}

}  // namespace

bool SaveOutputDir(const std::wstring& path, const std::wstring& directory, std::wstring& error) {
    if (directory.empty()) {
        error = L"保存路径为空";
        return false;
    }
    return WriteProfileVerified(path, kSectionOutput, L"dir", directory, error);
}

bool SaveTemplate(const std::wstring& path,
                  const std::wstring& filename_template,
                  std::wstring& error) {
    if (filename_template.empty()) {
        error = L"命名模板为空";
        return false;
    }
    return WriteProfileVerified(path, kSectionOutput, L"template", filename_template, error);
}

bool SaveRegion(const std::wstring& path, const CaptureRegion& region, std::wstring& error) {
    if (!region.enabled()) {
        return WriteProfileVerified(path, kSectionRecord, L"region", L"full", error);
    }
    wchar_t text[128] = {};
    std::swprintf(text, std::size(text), L"%d,%d,%d,%d",
                  region.x, region.y, region.width, region.height);
    return WriteProfileVerified(path, kSectionRecord, L"region", text, error);
}

bool SaveVideoSettings(const std::wstring& path,
                       uint32_t fps,
                       const std::wstring& rate_mode,
                       std::wstring& error) {
    if (fps < 1 || fps > 144) {
        error = L"帧率超出 1~144: " + std::to_wstring(fps);
        return false;
    }
    const std::wstring mode = ToLower(Trim(rate_mode));
    if (mode != L"cbr" && mode != L"vbr") {
        error = L"码率模式无效: " + rate_mode + L"（cbr/vbr）";
        return false;
    }
    if (!WriteProfileVerified(path, kSectionVideo, L"fps", std::to_wstring(fps), error)) {
        return false;
    }
    return WriteProfileVerified(path, kSectionVideo, L"rate_mode", mode, error);
}

// M3-10：四档画质预设。数值是产品自己的联动表，判据是「切换档位后各参数联动变化」，
// 不是某个行业固定值；「自定义」由高级面板（现有帧率/码率模式控件）单独覆盖。
static const wchar_t* const kQualityPresetNames[] = {
    L"极致", L"高清", L"标准", L"省空间", L"自定义"
};

const wchar_t* const* QualityPresetNames() { return kQualityPresetNames; }
size_t QualityPresetCount() { return std::size(kQualityPresetNames); }

bool ApplyQualityPreset(const std::wstring& name, AppConfig& config) {
    QualityPresetValues values;
    if (name == L"极致") {
        values = {60, 12000, 90};
    } else if (name == L"高清") {
        values = {30, 6000, 70};
    } else if (name == L"标准") {
        values = {30, 4000, 60};
    } else if (name == L"省空间") {
        values = {15, 2000, 50};
    } else if (name == L"自定义") {
        config.quality_preset = name;   // 不覆盖已有参数：高级面板单独改
        return true;
    } else {
        return false;
    }
    config.quality_preset = name;
    config.video_fps = values.fps;
    config.video_bitrate_kbps = values.bitrate_kbps;
    config.quality = values.quality;
    return true;
}

bool SaveQualityPreset(const std::wstring& path,
                       const std::wstring& name,
                       uint32_t fps,
                       uint32_t bitrate_kbps,
                       uint32_t quality,
                       std::wstring& error) {
    if (!WriteProfileVerified(path, kSectionVideo, L"quality_preset", name, error)) {
        return false;
    }
    if (!WriteProfileVerified(path, kSectionVideo, L"quality", std::to_wstring(quality), error)) {
        return false;
    }
    // 码率写在 [output] bitrate_kbps：与 LoadConfig 读取的键、RecorderController 下发的值同一个。
    if (!WriteProfileVerified(path, kSectionOutput, L"bitrate_kbps",
                              std::to_wstring(bitrate_kbps), error)) {
        return false;
    }
    return WriteProfileVerified(path, kSectionVideo, L"fps", std::to_wstring(fps), error);
}

bool SaveHotkey(const std::wstring& path,
                const std::wstring& action,
                const HotkeyCombo& combo,
                std::wstring& error) {
    if (action != L"start" && action != L"pause" && action != L"stop") {
        error = L"未知热键动作: " + action;
        return false;
    }
    if (combo.empty()) {
        error = L"热键组合为空";
        return false;
    }
    if (!EnsureParentDirectory(path, error)) {
        return false;
    }
    const std::wstring text = FormatHotkey(combo);
    if (!WritePrivateProfileStringW(kSectionHotkeys, action.c_str(), text.c_str(), path.c_str())) {
        error = L"写入 INI 失败, GetLastError=" + std::to_wstring(GetLastError());
        return false;
    }
    // 刷新缓存，随后回读校验，避免「写了但没落盘」的假成功。
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    const std::wstring read_back = ReadProfile(path, kSectionHotkeys, action.c_str(), L"");
    if (read_back != text) {
        error = L"INI 回读不一致: 期望 " + text + L"，实际 " + read_back;
        return false;
    }
    error.clear();
    return true;
}

}  // namespace sr::app

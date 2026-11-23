// 轮 55（0.2-1）：工作台骨架 —— Header(48) / 内容区（左列表占位 + 右设置面板 320）/ 状态栏(32)。
// 纯视图：不持有采集/编码对象，所有动作通过命令 ID 回到 main.cpp 的处理函数。
#pragma once

#include <windows.h>

#include <string>

#include "app/ui/controls.h"
#include "app/ui/draw.h"
#include "app/ui/theme.h"

namespace sr::ui {

struct WorkbenchModel {
    std::wstring status_text;   // 状态栏文本（可分两行，\n 分隔）
    std::wstring header_state;  // 药丸文字：就绪 / 启动中 / 录制中 / 已暂停
    PillTone header_tone = PillTone::Neutral;
    std::wstring warn_text;     // 提权限制常驻提示（空 = 隐藏提示行）
    std::wstring output_dir;
    std::wstring filename_template;
    std::wstring fps_text;
    std::wstring bitrate_text;
    std::wstring encoder_text;
    std::wstring log_level_text;
    std::wstring region_text;
    std::wstring version_text;
    std::wstring remaining_text;
    int recent_count = 0;       // 0 → 列表卡片显示空态文案
};

// 工作台：创建/布局/绘制所有自绘子控件与外壳。
class Workbench {
 public:
    bool Create(HWND parent);
    void SetModel(const WorkbenchModel& model);
    void Layout(const RECT& client, bool warn_visible);

    // 外壳绘制（底 / Header / 卡片 / 状态栏）。子控件各自绘制自己。
    void Paint(HDC dc, const RECT& client);

    bool HandleKeyDown(WPARAM vk);  // Tab / Shift+Tab / Enter / Esc
    void FocusNext(bool backward);
    void ToggleSettings();
    bool SettingsOpen() const { return settings_open_; }
    bool AutoCollapsed() const { return auto_collapsed_; }
    void SetSettingsOpen(bool open);

    RECT HeaderRect() const { return header_; }
    RECT StatusRect() const { return status_; }
    RECT ListCardRect() const { return list_card_; }
    RECT PanelRect() const { return panel_; }
    RECT PrimaryButtonRect() const;
    RECT ControlRect(int id) const;  // 子控件矩形（客户区坐标）
    HWND Parent() const { return parent_; }

    // 验收探针：坐标写日志，颜色由截图采样判定（不用日志当证据）。
    std::wstring LayoutLine() const;
    std::wstring ProbeLine() const;

 private:
    void PaintShell(Canvas& canvas, const RECT& client);
    static void PaintThunk(Canvas& canvas, const RECT& client, void* context);
    HWND CreateChildButton(int id, const std::wstring& text, ButtonKind kind, Surface surface);
    void PlaceControl(HWND control, const RECT& rc);
    void LayoutHeader();
    void LayoutWarnRow();
    void LayoutActionRow(const RECT& row);
    void LayoutPanel();
    void LayoutStatus();

    HWND parent_ = nullptr;
    WorkbenchModel model_;
    bool created_ = false;
    bool settings_open_ = true;
    bool auto_collapsed_ = false;
    bool warn_visible_ = false;

    // 子控件
    HWND pill_ = nullptr;          // 状态药丸
    HWND status_carrier_ = nullptr;// 状态文本载体（不可见 STATIC，id=200）
    HWND start_ = nullptr;
    HWND pause_ = nullptr;
    HWND stop_ = nullptr;
    HWND save_path_ = nullptr;
    HWND region_ = nullptr;
    HWND list_ = nullptr;
    HWND settings_ = nullptr;
    HWND panel_toggle_ = nullptr;
    HWND hide_ = nullptr;
    HWND elevate_ = nullptr;
    HWND warn_ = nullptr;
    HWND fields_[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

    // 几何
    RECT client_rc_{};
    RECT header_{};
    RECT title_rc_{};
    RECT status_{};
    RECT body_{};
    RECT warn_row_{};
    RECT content_{};
    RECT action_row_{};
    RECT list_card_{};
    RECT panel_{};
    RECT version_rc_{};
    bool layout_valid_ = false;
};

}  // namespace sr::ui

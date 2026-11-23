#include "app/ui/workbench.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <vector>

#include "app/ui/ids.h"
#include "app/ui/layout.h"

namespace sr::ui {
namespace {

constexpr int kEdgeProbeInset = 20;

bool IsTabStop(HWND hwnd) {
    return hwnd != nullptr && IsWindowVisible(hwnd) && IsWindowEnabled(hwnd) &&
           (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_TABSTOP) != 0;
}

std::wstring Format(const wchar_t* format, ...) {
    wchar_t buffer[512] = {};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, _TRUNCATE, format, args);
    va_end(args);
    return buffer;
}

}  // namespace

HWND Workbench::CreateChildButton(int id, const std::wstring& text, ButtonKind kind,
                                 Surface surface) {
    ButtonDesc desc;
    desc.id = id;
    desc.text = text;
    desc.kind = kind;
    desc.surface = surface;
    return CreateButton(parent_, desc, MakeRect(0, 0, 1, 1));
}

bool Workbench::Create(HWND parent) {
    parent_ = parent;
    pill_ = CreatePill(parent, PillDesc{kHeaderPill, L"就绪", PillTone::Neutral, Surface::Header},
                       MakeRect(0, 0, 1, 1));
    // 状态文本载体：不可见 STATIC，保持既有自动化（GetDlgItem(main,200)）与无障碍读取能力。
    status_carrier_ = CreateWindowExW(0, L"STATIC", L"就绪", WS_CHILD, 0, 0, 1, 1, parent,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusId)),
                                      reinterpret_cast<HINSTANCE>(
                                          GetWindowLongPtrW(parent, GWLP_HINSTANCE)),
                                      nullptr);
    start_ = CreateChildButton(kStartButton, L"开始录制", ButtonKind::Primary, Surface::Base);
    pause_ = CreateChildButton(kPauseButton, L"暂停", ButtonKind::Secondary, Surface::Base);
    stop_ = CreateChildButton(kStopButton, L"停止", ButtonKind::Secondary, Surface::Base);
    save_path_ = CreateChildButton(kSavePathButton, L"保存路径…", ButtonKind::Ghost, Surface::Base);
    region_ = CreateChildButton(kRegionButton, L"区域…", ButtonKind::Ghost, Surface::Base);
    list_ = CreateChildButton(kListButton, L"录制列表…", ButtonKind::Ghost, Surface::Base);
    settings_ = CreateChildButton(kSettingsButton, L"设置…", ButtonKind::Ghost, Surface::Header);
    panel_toggle_ =
        CreateChildButton(kPanelToggleButton, L"设置面板", ButtonKind::Ghost, Surface::Header);
    hide_ = CreateChildButton(kHideButton, L"最小化到托盘", ButtonKind::Ghost, Surface::Header);
    elevate_ = CreateChildButton(kElevateButton, L"以管理员身份重启", ButtonKind::Secondary,
                                 Surface::Warn);
    warn_ = CreateLabel(parent, LabelDesc{kUacWarnId, L"", true, Surface::Warn},
                        MakeRect(0, 0, 1, 1));
    const int field_ids[6] = {kFieldSavePath, kFieldTemplate, kFieldFps,
                              kFieldBitrate,  kFieldEncoder,  kFieldLogLevel};
    const wchar_t* field_labels[6] = {L"保存位置", L"命名模板", L"帧率",
                                      L"码率",     L"编码器",   L"日志级别"};
    for (int i = 0; i < 6; ++i) {
        FieldDesc desc;
        desc.id = field_ids[i];
        desc.label = field_labels[i];
        desc.surface = Surface::Card;
        fields_[i] = CreateField(parent, desc, MakeRect(0, 0, 1, 1));
    }
    created_ = start_ != nullptr && status_carrier_ != nullptr;
    return created_;
}

void Workbench::PlaceControl(HWND control, const RECT& rc) {
    if (control == nullptr) {
        return;
    }
    SetWindowPos(control, nullptr, rc.left, rc.top, std::max(1, RectWidth(rc)),
                 std::max(1, RectHeight(rc)), SWP_NOZORDER | SWP_NOACTIVATE);
}

void Workbench::SetModel(const WorkbenchModel& model) {
    model_ = model;
    if (pill_ != nullptr) {
        SetControlText(pill_, model_.header_state);
        SetPillTone(pill_, model_.header_tone);
    }
    if (status_carrier_ != nullptr) {
        SetWindowTextW(status_carrier_, model_.status_text.c_str());
    }
    if (warn_ != nullptr) {
        SetControlText(warn_, model_.warn_text);
    }
    const std::wstring values[6] = {model_.output_dir,  model_.filename_template, model_.fps_text,
                                    model_.bitrate_text, model_.encoder_text, model_.log_level_text};
    for (int i = 0; i < 6; ++i) {
        SetFieldValue(fields_[i], values[i]);
    }
    if (layout_valid_ && parent_ != nullptr) {
        InvalidateRect(parent_, nullptr, FALSE);
    }
}

void Workbench::SetSettingsOpen(bool open) {
    settings_open_ = open;
    if (parent_ != nullptr && layout_valid_) {
        RECT client{};
        GetClientRect(parent_, &client);
        Layout(client, warn_visible_);
        InvalidateRect(parent_, nullptr, FALSE);
    }
}

void Workbench::ToggleSettings() { SetSettingsOpen(!settings_open_); }

void Workbench::Layout(const RECT& client, bool warn_visible) {
    if (!created_) {
        return;
    }
    warn_visible_ = warn_visible;
    client_rc_ = client;
    const int width = RectWidth(client);
    const int height = RectHeight(client);
    if (width <= 0 || height <= 0) {
        return;
    }
    const Theme& t = Current();
    const int pad = Scale(t.space_l);
    const int gap = Scale(t.space_m);

    header_ = MakeRect(client.left, client.top, width, Scale(t.header_h));
    status_ = MakeRect(client.left, client.bottom - Scale(t.status_h), width, Scale(t.status_h));
    body_ = MakeRect(client.left, header_.bottom, width, status_.top - header_.bottom);

    int body_top = body_.top;
    warn_row_ = MakeRect(0, 0, 0, 0);
    if (warn_visible_) {
        const int warn_h = Scale(34);
        warn_row_ = MakeRect(client.left + pad, body_top + Scale(t.space_s),
                             width - pad * 2, warn_h);
        body_top = warn_row_.bottom + Scale(t.space_s);
    }

    // 最小窗口下右侧面板自动收起（860×560 客户区明显小于 900dp）。
    const int collapse_width = Scale(900);
    auto_collapsed_ = width < collapse_width;
    const bool panel_visible = settings_open_ && !auto_collapsed_;

    content_ = MakeRect(client.left + pad, body_top, width - pad * 2,
                        body_.bottom - body_top - Scale(t.space_m));
    if (RectHeight(content_) < Scale(80)) {
        content_ = MakeRect(client.left + pad, body_top, width - pad * 2,
                            std::max(Scale(80), static_cast<int>(status_.top - body_top - Scale(4))));
    }

    panel_ = MakeRect(0, 0, 0, 0);
    RECT left = content_;
    const int panel_w = Scale(t.panel_w);
    if (panel_visible && RectWidth(content_) > panel_w + Scale(240)) {
        panel_ = MakeRect(content_.right - panel_w, content_.top, panel_w, RectHeight(content_));
        left.right = panel_.left - gap;
    }

    action_row_ = MakeRect(left.left, left.top, RectWidth(left), Scale(t.button_h));
    list_card_ = MakeRect(left.left, action_row_.bottom + gap, RectWidth(left),
                          left.bottom - action_row_.bottom - gap);
    if (RectHeight(list_card_) < Scale(60)) {
        list_card_ = MakeRect(left.left, action_row_.bottom, RectWidth(left),
                              std::max(Scale(60), static_cast<int>(left.bottom - action_row_.bottom)));
    }

    LayoutHeader();
    LayoutActionRow(action_row_);
    LayoutWarnRow();
    LayoutPanel();
    LayoutStatus();
    layout_valid_ = true;
}

void Workbench::LayoutWarnRow() {
    if (RectEmpty(warn_row_)) {
        ShowWindow(warn_, SW_HIDE);
        ShowWindow(elevate_, SW_HIDE);
        return;
    }
    const Theme& t = Current();
    const int pad = Scale(t.space_m);
    HStack stack(Inset(warn_row_, pad, 0), Scale(t.space_m));
    RECT button = stack.NextRight(Scale(148));
    PlaceControl(elevate_, Inset(button, 0, Scale(3)));
    PlaceControl(warn_, stack.Remaining());
    ShowWindow(warn_, SW_SHOWNA);
    ShowWindow(elevate_, SW_SHOWNA);
}

void Workbench::LayoutHeader() {
    const Theme& t = Current();
    const int pad = Scale(t.space_l);
    const int gap = Scale(t.space_m);
    HStack header_stack(MakeRect(header_.left + pad, header_.top, RectWidth(header_) - pad * 2,
                                 RectHeight(header_)),
                        gap);
    RECT hide_rc = header_stack.NextRight(Scale(104));
    RECT toggle_rc = header_stack.NextRight(Scale(84));
    RECT settings_rc = header_stack.NextRight(Scale(64));
    PlaceControl(hide_, Inset(hide_rc, 0, Scale(6)));
    PlaceControl(panel_toggle_, Inset(toggle_rc, 0, Scale(6)));
    PlaceControl(settings_, Inset(settings_rc, 0, Scale(6)));

    HStack left_stack(MakeRect(header_.left + pad, header_.top, header_stack.Available() - gap,
                               RectHeight(header_)),
                      gap);
    // 标题由外壳直接绘制（文本色 text_primary），不额外占一个子窗口。
    title_rc_ = left_stack.Next(Scale(150));
    PlaceControl(pill_, Inset(left_stack.Next(Scale(96)), 0, Scale(13)));
}

void Workbench::LayoutActionRow(const RECT& row) {
    const Theme& t = Current();
    const int gap = Scale(t.space_s);
    HStack stack(row, gap);
    const int widths[6] = {Scale(132), Scale(84), Scale(84), Scale(112), Scale(78), Scale(110)};
    HWND controls[6] = {start_, pause_, stop_, save_path_, region_, list_};
    for (int i = 0; i < 6; ++i) {
        PlaceControl(controls[i], stack.Next(widths[i]));
    }
}

void Workbench::LayoutPanel() {
    if (RectEmpty(panel_)) {
        for (int i = 0; i < 6; ++i) {
            ShowWindow(fields_[i], SW_HIDE);
        }
        return;
    }
    const Theme& t = Current();
    const int pad = Scale(t.space_l);
    RECT inner = Inset(panel_, pad);
    const int field_h = Scale(t.space_l + t.font_caption * 3 / 2 + t.field_h);
    const int gap = Scale(t.space_m);
    int y = inner.top;
    for (int i = 0; i < 6; ++i) {
        const int remaining = inner.bottom - y;
        if (remaining < field_h) {
            ShowWindow(fields_[i], SW_HIDE);
            continue;
        }
        PlaceControl(fields_[i], MakeRect(inner.left, y, RectWidth(inner), field_h));
        ShowWindow(fields_[i], SW_SHOWNA);
        y += field_h + gap;
    }
}

void Workbench::LayoutStatus() {
    const Theme& t = Current();
    const int pad = Scale(t.space_l);
    version_rc_ = MakeRect(status_.right - pad - Scale(120), status_.top, Scale(120),
                           RectHeight(status_));
}

RECT Workbench::PrimaryButtonRect() const { return ControlRect(kStartButton); }

RECT Workbench::ControlRect(int id) const {
    RECT rc{};
    HWND control = GetDlgItem(parent_, id);
    if (control == nullptr) {
        return rc;
    }
    GetWindowRect(control, &rc);
    POINT origin{rc.left, rc.top};
    ScreenToClient(parent_, &origin);
    return MakeRect(origin.x, origin.y, rc.right - rc.left, rc.bottom - rc.top);
}

void Workbench::PaintThunk(Canvas& canvas, const RECT& client, void* context) {
    static_cast<Workbench*>(context)->PaintShell(canvas, client);
}

void Workbench::Paint(HDC dc, const RECT& client) {
    if (!layout_valid_) {
        FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return;
    }
    DrawBuffered(dc, client, PaintThunk, this);
}

void Workbench::PaintShell(Canvas& canvas, const RECT& client) {
    const Theme& t = Current();
    const int pad = Scale(t.space_l);

    // 底
    canvas.FillRect(client, t.base);

    // Header + 标题（标题是外壳绘制的静态文本，避免多一个子窗口铺底）
    canvas.FillRect(header_, t.header);
    TextStyle title_style;
    title_style.size = Scale(t.font_title);
    title_style.weight = FontWeight::Semibold;
    canvas.Text(L"Screen Recorder", title_rc_, title_style, t.text_primary);

    // 提权提示行（仅未提权时）
    if (!RectEmpty(warn_row_)) {
        canvas.FillRoundRect(warn_row_, Scale(t.radius_control),
                             Blend(t.base, t.warning, 10));
    }

    // 左侧成片列表卡片
    if (!RectEmpty(list_card_)) {
        DrawCard(canvas, list_card_);
        RECT title_rc = MakeRect(list_card_.left + pad, list_card_.top + Scale(t.space_m),
                                 RectWidth(list_card_) - pad * 2, Scale(t.font_title * 2));
        DrawCardTitle(canvas, title_rc, L"最近录制");
        RECT sub_rc = MakeRect(title_rc.left, title_rc.bottom, RectWidth(title_rc),
                               Scale(t.font_caption * 2));
        TextStyle caption;
        caption.size = Scale(t.font_caption);
        canvas.Text(model_.recent_count > 0 ? Format(L"%d 个文件", model_.recent_count)
                                            : L"还没有录制文件",
                    sub_rc, caption, t.text_secondary);
        RECT empty_rc = MakeRect(list_card_.left + pad, sub_rc.bottom + Scale(t.space_s),
                                 RectWidth(list_card_) - pad * 2,
                                 list_card_.bottom - sub_rc.bottom - Scale(t.space_l));
        if (model_.recent_count <= 0 && !RectEmpty(empty_rc)) {
            DrawEmptyState(canvas, empty_rc, L"空态：录制完成后成片会列在这里",
                           L"点击「开始录制」，或按 Ctrl+Alt+F9");
        }
    }

    // 右侧设置面板卡片
    if (!RectEmpty(panel_)) {
        DrawCard(canvas, panel_);
        RECT title_rc = MakeRect(panel_.left + pad, panel_.top + Scale(t.space_m),
                                 RectWidth(panel_) - pad * 2, Scale(t.font_title * 2));
        DrawCardTitle(canvas, title_rc, L"设置");
    }

    // 状态栏：底 + 两行状态文本 + 右侧版本号
    canvas.FillRect(status_, t.statusbar);
    RECT status_text = MakeRect(status_.left + pad, status_.top,
                                RectWidth(status_) - pad * 2 - Scale(140), RectHeight(status_));
    TextStyle status_style;
    status_style.size = Scale(t.font_caption);
    const int line = Scale(t.font_caption * 3 / 2);
    canvas.TextLines(model_.status_text, status_text, status_style, t.text_secondary, line);
    TextStyle version_style;
    version_style.size = Scale(t.font_caption);
    version_style.align = TextAlign::Right;
    canvas.Text(model_.version_text, version_rc_, version_style, t.text_disabled);
}

void Workbench::FocusNext(bool backward) {
    // Tab 顺序按「视觉阅读顺序」（先上后下、同行先左后右），而不是子窗口的 Z 序——
    // Z 序随创建/激活顺序变化，用户按 Tab 会跳来跳去。
    struct Stop {
        HWND hwnd = nullptr;
        int top = 0;
        int left = 0;
    };
    std::vector<Stop> stops;
    for (HWND child = GetWindow(parent_, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        if (!IsTabStop(child)) {
            continue;
        }
        RECT rc{};
        GetWindowRect(child, &rc);
        stops.push_back(Stop{child, static_cast<int>(rc.top), static_cast<int>(rc.left)});
    }
    std::sort(stops.begin(), stops.end(), [](const Stop& a, const Stop& b) {
        if (a.top / 16 != b.top / 16) {
            return a.top < b.top;
        }
        return a.left < b.left;
    });
    if (stops.empty()) {
        return;
    }
    HWND focused = GetFocus();
    size_t index = stops.size();
    for (size_t i = 0; i < stops.size(); ++i) {
        if (stops[i].hwnd == focused) {
            index = i;
            break;
        }
    }
    size_t next = 0;
    if (index < stops.size()) {
        next = backward ? (index + stops.size() - 1) % stops.size() : (index + 1) % stops.size();
    }
    SetFocus(stops[next].hwnd);
}

bool Workbench::HandleKeyDown(WPARAM vk) {
    if (vk == VK_TAB) {
        FocusNext((GetKeyState(VK_SHIFT) & 0x8000) != 0);
        return true;
    }
    if (vk == VK_RETURN) {
        HWND focused = GetFocus();
        if (focused != nullptr && GetParent(focused) == parent_) {
            SendMessageW(focused, BM_CLICK, 0, 0);
        } else {
            SendMessageW(start_, BM_CLICK, 0, 0);
        }
        return true;
    }
    if (vk == VK_ESCAPE) {
        if (settings_open_ && !auto_collapsed_) {
            SetSettingsOpen(false);
            return true;
        }
    }
    return false;
}

std::wstring Workbench::LayoutLine() const {
    const Theme& t = Current();
    return Format(
        L"ui_layout client=%dx%d dpi=%u scale=%.2f header_h=%d status_h=%d panel_w=%d "
        L"panel_open=%d warn_row=%d list_card=%dx%d theme=%ls",
        RectWidth(client_rc_), RectHeight(client_rc_), Dpi(), ScaleFactor(),
        RectHeight(header_), RectHeight(status_), RectWidth(panel_),
        RectEmpty(panel_) ? 0 : 1, RectEmpty(warn_row_) ? 0 : 1, RectWidth(list_card_),
        RectHeight(list_card_), t.name);
}

std::wstring Workbench::ProbeLine() const {
    RECT client{};
    if (parent_ != nullptr) {
        GetClientRect(parent_, &client);
    }
    const Theme& t = Current();
    const int pad = Scale(t.space_l);

    // 底色探针：左侧栏与右侧面板之间的空隙；面板收起时取右侧留白。
    int base_x = static_cast<int>(client.right) - std::max(Scale(4), pad / 2);
    int base_y = content_.top + RectHeight(content_) / 2;
    if (!RectEmpty(panel_)) {
        base_x = static_cast<int>(list_card_.right) +
                 std::max(1, static_cast<int>(panel_.left - list_card_.right) / 2);
        base_y = list_card_.bottom - Scale(kEdgeProbeInset);
    }

    // 卡片探针：列表卡片内部左下角
    const int card_x = list_card_.left + pad;
    const int card_y = list_card_.bottom - Scale(kEdgeProbeInset);

    // 主按钮探针：中心点
    RECT primary = PrimaryButtonRect();
    const int primary_x = primary.left + RectWidth(primary) / 2;
    const int primary_y = primary.top + RectHeight(primary) / 2;

    // 文本探针：Header 标题区域（测试取该区域最亮像素判文本色）
    const int text_x = header_.left + pad;
    const int text_y = header_.top + Scale(6);
    const int text_w = Scale(180);
    const int text_h = Scale(t.header_h - 12);

    // 三处描边采样：列表卡片左/上/下边
    const RECT border1 =
        MakeRect(list_card_.left, list_card_.top + RectHeight(list_card_) / 2, 1, 1);
    const RECT border2 =
        MakeRect(list_card_.left + RectWidth(list_card_) / 2, list_card_.top, 1, 1);
    const RECT border3 = RectEmpty(panel_)
                             ? MakeRect(list_card_.left, list_card_.top + RectHeight(list_card_) / 3,
                                        1, 1)
                             : MakeRect(panel_.left, panel_.top + RectHeight(panel_) / 2, 1, 1);

    // 面板探针：按「面板展开时」的名义位置取样（第一个字段的值框内部）。
    // 面板展开 → 值框底色 base；面板收起 → 该处变成列表卡片 card，两态像素可区分。
    const int nominal_panel_left = static_cast<int>(content_.right) - Scale(t.panel_w);
    const int panel_x = nominal_panel_left + Scale(48);
    const int panel_y = static_cast<int>(content_.top) + Scale(72);

    return Format(
        L"ui_probe base=%d,%d card=%d,%d primary=%d,%d text=%d,%d,%d,%d "
        L"border1=%d,%d border2=%d,%d border3=%d,%d panel=%d,%d",
        base_x, base_y, card_x, card_y, primary_x, primary_y, text_x, text_y, text_w, text_h,
        border1.left, border1.top, border2.left, border2.top, border3.left, border3.top, panel_x,
        panel_y);
}

}  // namespace sr::ui

#include "win32_ui.h"

#include "backtest_service.h"

#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
constexpr COLORREF kBackground = RGB(246, 248, 251);
constexpr COLORREF kCard = RGB(255, 255, 255);
constexpr COLORREF kBorder = RGB(222, 228, 236);
constexpr COLORREF kText = RGB(36, 48, 65);
constexpr COLORREF kMuted = RGB(102, 116, 136);
constexpr COLORREF kPrimary = RGB(33, 124, 216);
constexpr COLORREF kPrimaryHover = RGB(26, 103, 184);
constexpr COLORREF kPrimarySoft = RGB(226, 239, 251);
constexpr COLORREF kPositive = RGB(35, 139, 85);
constexpr COLORREF kNegative = RGB(201, 64, 64);
constexpr COLORREF kGrid = RGB(229, 234, 241);

enum : int {
    ID_MODE_BOLL = 100, ID_MODE_MACD, ID_MAIN, ID_CONTRACT, ID_BUFFER,
    ID_START, ID_END, ID_EXCHANGE, ID_RUN, ID_EXPORT, ID_LOG, ID_PROGRESS,
    WM_BACKTEST_RESULT = WM_APP + 1
};

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), count);
    result.resize(static_cast<std::size_t>(count - 1));
    return result;
}

std::string narrow(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), count, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(count - 1));
    return result;
}

std::string text_utf8(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(length + 1), L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return narrow(value);
}

void set_text(HWND control, const std::wstring& value) {
    SetWindowTextW(control, value.c_str());
}

HFONT make_font(int height, int weight = FW_NORMAL) {
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
}

std::wstring number(double value, int precision = 2) {
    std::wostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

void rounded_card(HDC dc, const RECT& rect, int radius = 12) {
    auto fill = CreateSolidBrush(kCard);
    auto pen = CreatePen(PS_SOLID, 1, kBorder);
    const auto old_brush = SelectObject(dc, fill);
    const auto old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(fill);
}

class ChartView {
public:
    explicit ChartView(HINSTANCE instance) : instance_(instance), font_(make_font(12)) {}
    ~ChartView() { if (font_) DeleteObject(font_); }

    HWND create(HWND parent) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &ChartView::proc;
        wc.hInstance = instance_;
        wc.lpszClassName = L"ArbitrageTool.Chart";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"", WS_CHILD,
            0, 0, 100, 100, parent, nullptr, instance_, this);
        return hwnd_;
    }

    HWND hwnd() const { return hwnd_; }

    void set_data(const BacktestRun& run) {
        equity_ = run.analytics.equity_curve;
        drawdown_ = run.analytics.drawdown_curve;
        dates_.clear();
        dates_.reserve(run.result.rows.size());
        for (const auto& row : run.result.rows) dates_.push_back(wide(row.date));
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

private:
    HINSTANCE instance_{};
    HWND hwnd_{};
    HFONT font_{};
    std::vector<double> equity_;
    std::vector<double> drawdown_;
    std::vector<std::wstring> dates_;

    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<ChartView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<ChartView*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
        if (self && message == WM_PAINT) { self->paint(); return 0; }
        return DefWindowProcW(hwnd, message, wp, lp);
    }

    void paint() {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd_, &ps);
        RECT client{}; GetClientRect(hwnd_, &client);
        FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kText);
        const auto old_font = SelectObject(dc, font_);

        RECT title{16, 10, client.right - 16, 34};
        DrawTextW(dc, L"累计盈亏与回撤", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (equity_.empty()) {
            SetTextColor(dc, kMuted);
            RECT empty{16, 38, client.right - 16, client.bottom - 14};
            DrawTextW(dc, L"完成一次回测后，这里会显示累计盈亏和回撤曲线。", -1, &empty, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, old_font);
            EndPaint(hwnd_, &ps);
            return;
        }

        RECT plot{52, 44, std::max(80L, client.right - 18), std::max(72L, client.bottom - 32)};
        double minimum = 0;
        double maximum = 0;
        for (double v : equity_) { minimum = std::min(minimum, v); maximum = std::max(maximum, v); }
        for (double v : drawdown_) { minimum = std::min(minimum, v); maximum = std::max(maximum, v); }
        if (std::abs(maximum - minimum) < 1e-9) { maximum += 1; minimum -= 1; }

        auto grid_pen = CreatePen(PS_SOLID, 1, kGrid);
        const auto old_pen = SelectObject(dc, grid_pen);
        SetTextColor(dc, kMuted);
        for (int i = 0; i <= 4; ++i) {
            const int y = plot.top + (plot.bottom - plot.top) * i / 4;
            MoveToEx(dc, plot.left, y, nullptr); LineTo(dc, plot.right, y);
            const double value = maximum - (maximum - minimum) * i / 4.0;
            RECT label{2, y - 9, plot.left - 7, y + 9};
            const auto value_text = number(value, 1);
            DrawTextW(dc, value_text.c_str(), -1, &label, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }

        auto point_for = [&](std::size_t index, double value) {
            const double x_ratio = equity_.size() <= 1 ? 0 : static_cast<double>(index) / static_cast<double>(equity_.size() - 1);
            const double y_ratio = (maximum - value) / (maximum - minimum);
            POINT point{};
            point.x = plot.left + static_cast<LONG>((plot.right - plot.left) * x_ratio);
            point.y = plot.top + static_cast<LONG>((plot.bottom - plot.top) * y_ratio);
            return point;
        };

        std::vector<POINT> equity_points; equity_points.reserve(equity_.size());
        std::vector<POINT> drawdown_points; drawdown_points.reserve(drawdown_.size());
        for (std::size_t i = 0; i < equity_.size(); ++i) equity_points.push_back(point_for(i, equity_[i]));
        for (std::size_t i = 0; i < drawdown_.size(); ++i) drawdown_points.push_back(point_for(i, drawdown_[i]));

        auto equity_pen = CreatePen(PS_SOLID, 2, kPrimary);
        SelectObject(dc, equity_pen);
        if (equity_points.size() > 1) Polyline(dc, equity_points.data(), static_cast<int>(equity_points.size()));
        auto drawdown_pen = CreatePen(PS_SOLID, 2, kNegative);
        SelectObject(dc, drawdown_pen);
        if (drawdown_points.size() > 1) Polyline(dc, drawdown_points.data(), static_cast<int>(drawdown_points.size()));

        SelectObject(dc, old_pen);
        DeleteObject(drawdown_pen);
        DeleteObject(equity_pen);
        DeleteObject(grid_pen);

        SetTextColor(dc, kMuted);
        if (!dates_.empty()) {
            RECT left{plot.left, plot.bottom + 5, plot.left + 130, plot.bottom + 24};
            RECT middle{(plot.left + plot.right) / 2 - 65, plot.bottom + 5, (plot.left + plot.right) / 2 + 65, plot.bottom + 24};
            RECT right{plot.right - 130, plot.bottom + 5, plot.right, plot.bottom + 24};
            DrawTextW(dc, dates_.front().c_str(), -1, &left, DT_LEFT | DT_SINGLELINE);
            DrawTextW(dc, dates_[dates_.size() / 2].c_str(), -1, &middle, DT_CENTER | DT_SINGLELINE);
            DrawTextW(dc, dates_.back().c_str(), -1, &right, DT_RIGHT | DT_SINGLELINE);
        }

        auto legend_blue = CreateSolidBrush(kPrimary);
        RECT blue{client.right - 170, 14, client.right - 158, 26}; FillRect(dc, &blue, legend_blue); DeleteObject(legend_blue);
        SetTextColor(dc, kText);
        RECT blue_text{client.right - 152, 9, client.right - 88, 31}; DrawTextW(dc, L"累计盈亏", -1, &blue_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        auto legend_red = CreateSolidBrush(kNegative);
        RECT red{client.right - 80, 14, client.right - 68, 26}; FillRect(dc, &red, legend_red); DeleteObject(legend_red);
        RECT red_text{client.right - 62, 9, client.right - 10, 31}; DrawTextW(dc, L"回撤", -1, &red_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(dc, old_font);
        EndPaint(hwnd_, &ps);
    }
};

struct ResultPayload {
    bool ok = false;
    std::unique_ptr<BacktestRun> run;
    std::string error;
};

class MainWindow {
public:
    MainWindow(HINSTANCE instance, BacktestService& service)
        : instance_(instance), service_(service), chart_(instance), body_font_(make_font(14)),
          small_font_(make_font(12)), title_font_(make_font(22, FW_SEMIBOLD)),
          section_font_(make_font(15, FW_SEMIBOLD)), value_font_(make_font(20, FW_SEMIBOLD)),
          background_brush_(CreateSolidBrush(kBackground)), edit_brush_(CreateSolidBrush(kCard)) {}

    ~MainWindow() {
        for (auto font : {body_font_, small_font_, title_font_, section_font_, value_font_}) if (font) DeleteObject(font);
        if (background_brush_) DeleteObject(background_brush_);
        if (edit_brush_) DeleteObject(edit_brush_);
    }

    int show() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &MainWindow::proc;
        wc.hInstance = instance_;
        wc.lpszClassName = L"ArbitrageTool.Main";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = background_brush_;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassW(&wc);

        hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"套利回测工具 v2.0",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1120, 820,
            nullptr, nullptr, instance_, this);
        if (!hwnd_) { MessageBoxW(nullptr, L"主窗口创建失败。", L"启动失败", MB_OK | MB_ICONERROR); return 1; }
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd_);
        UpdateWindow(hwnd_);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(hwnd_, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        return static_cast<int>(message.wParam);
    }

private:
    HINSTANCE instance_{};
    BacktestService& service_;
    HWND hwnd_{};
    HWND title_{};
    HWND strategy_label_{}, main_label_{}, contract_label_{}, buffer_label_{}, exchange_label_{}, start_label_{}, end_label_{};
    HWND main_{}, contract_{}, buffer_{}, exchange_{}, start_{}, end_{};
    HWND boll_{}, macd_{}, run_{}, export_{}, log_{}, status_{}, progress_{};
    HWND kpi_heading_{}, log_heading_{};
    std::array<HWND, 4> kpi_names_{};
    std::array<HWND, 4> kpi_values_{};
    ChartView chart_;
    StrategyMode mode_ = StrategyMode::Bollinger;
    std::unique_ptr<BacktestRun> last_;
    bool running_ = false;
    std::chrono::steady_clock::time_point started_at_{};
    HFONT body_font_{}, small_font_{}, title_font_{}, section_font_{}, value_font_{};
    HBRUSH background_brush_{}, edit_brush_{};
    RECT config_card_{}, result_card_{};

    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
        return self ? self->handle(message, wp, lp) : DefWindowProcW(hwnd, message, wp, lp);
    }

    HWND control(const wchar_t* class_name, const wchar_t* text, DWORD style, int id = 0, DWORD ex_style = 0) {
        HWND control_hwnd = CreateWindowExW(ex_style, class_name, text, WS_CHILD | WS_VISIBLE | style,
            0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SendMessageW(control_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(body_font_), TRUE);
        return control_hwnd;
    }

    HWND label(const wchar_t* text, HFONT font = nullptr, DWORD style = SS_LEFT | SS_CENTERIMAGE) {
        HWND result = control(L"STATIC", text, style);
        if (font) SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return result;
    }

    void create_controls() {
        title_ = label(L"套利回测工具", title_font_, SS_CENTER | SS_CENTERIMAGE);
        strategy_label_ = label(L"策略");
        main_label_ = label(L"主连代码");
        contract_label_ = label(L"期货代码");
        buffer_label_ = label(L"滑点缓冲");
        exchange_label_ = label(L"交易所");
        start_label_ = label(L"开始日期");
        end_label_ = label(L"结束日期");

        boll_ = control(L"BUTTON", L"BOLL", BS_OWNERDRAW | WS_TABSTOP, ID_MODE_BOLL);
        macd_ = control(L"BUTTON", L"MACD", BS_OWNERDRAW | WS_TABSTOP, ID_MODE_MACD);
        main_ = control(L"EDIT", L"RB.SHFE", ES_AUTOHSCROLL | WS_TABSTOP, ID_MAIN, WS_EX_CLIENTEDGE);
        contract_ = control(L"EDIT", L"I.DCE", ES_AUTOHSCROLL | WS_TABSTOP, ID_CONTRACT, WS_EX_CLIENTEDGE);
        buffer_ = control(L"EDIT", L"0", ES_AUTOHSCROLL | WS_TABSTOP, ID_BUFFER, WS_EX_CLIENTEDGE);
        exchange_ = control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, ID_EXCHANGE);
        for (const wchar_t* name : {L"郑商所", L"上期所", L"大商所", L"中金所", L"上期能源", L"广期所"})
            SendMessageW(exchange_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SendMessageW(exchange_, CB_SETCURSEL, 1, 0);

        start_ = control(DATETIMEPICK_CLASSW, L"", DTS_SHORTDATEFORMAT | WS_TABSTOP, ID_START);
        end_ = control(DATETIMEPICK_CLASSW, L"", DTS_SHORTDATEFORMAT | WS_TABSTOP, ID_END);
        SendMessageW(start_, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(const_cast<wchar_t*>(L"yyyy-MM-dd")));
        SendMessageW(end_, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(const_cast<wchar_t*>(L"yyyy-MM-dd")));
        SYSTEMTIME start_time{2020, 1, 0, 1, 0, 0, 0, 0};
        SYSTEMTIME end_time{2020, 12, 0, 31, 0, 0, 0, 0};
        SendMessageW(start_, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&start_time));
        SendMessageW(end_, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&end_time));

        run_ = control(L"BUTTON", L"▶  开始回测", BS_OWNERDRAW | WS_TABSTOP, ID_RUN);
        export_ = control(L"BUTTON", L"导出 CSV", BS_OWNERDRAW | WS_TABSTOP, ID_EXPORT);
        EnableWindow(export_, FALSE);

        kpi_heading_ = label(L"回测指标摘要", section_font_);
        log_heading_ = label(L"详细回测日志", section_font_);
        const std::array<const wchar_t*, 4> names{L"累计盈亏", L"成交次数", L"胜率", L"最大回撤"};
        for (std::size_t i = 0; i < names.size(); ++i) {
            kpi_names_[i] = label(names[i], small_font_);
            kpi_values_[i] = label(L"--", value_font_, SS_RIGHT | SS_CENTERIMAGE);
        }

        chart_.create(hwnd_);
        log_ = control(L"EDIT", L"等待回测任务...", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, ID_LOG, WS_EX_CLIENTEDGE);
        SendMessageW(log_, WM_SETFONT, reinterpret_cast<WPARAM>(small_font_), TRUE);
        status_ = label(L"就绪。主连代码非空时使用 AkShare；留空时使用 Tushare。", small_font_);
        progress_ = control(PROGRESS_CLASSW, L"", PBS_MARQUEE, ID_PROGRESS);
        ShowWindow(progress_, SW_HIDE);

        for (HWND item : {main_, contract_, buffer_, exchange_, start_, end_, log_}) SetWindowTheme(item, L"Explorer", nullptr);
        SendMessageW(main_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
        SendMessageW(contract_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
        SendMessageW(buffer_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
        set_mode(StrategyMode::Bollinger);
    }

    void move(HWND control_hwnd, int x, int y, int width, int height) {
        SetWindowPos(control_hwnd, nullptr, x, y, std::max(1, width), std::max(1, height), SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void layout(int width, int height) {
        const int margin = 24;
        const int content_width = std::max(760, width - margin * 2);
        move(title_, margin, 12, content_width, 42);

        config_card_ = RECT{margin, 66, width - margin, 314};
        result_card_ = RECT{margin, 330, width - margin, std::max(540, height - 54)};

        const int inner_left = margin + 18;
        const int inner_right = width - margin - 18;
        const int label_width = 86;
        const int gap = 24;
        const int action_width = 150;
        const int fields_right = inner_right - action_width - 22;
        const int field_area = fields_right - inner_left;
        const int column_width = (field_area - gap) / 2;
        const int edit_width = column_width - label_width - 10;
        const int left_x = inner_left;
        const int right_x = inner_left + column_width + gap;

        move(strategy_label_, left_x, 98, label_width, 34);
        move(boll_, left_x + label_width + 10, 98, 108, 36);
        move(macd_, left_x + label_width + 128, 98, 108, 36);
        move(run_, fields_right + 22, 104, action_width, 46);
        move(export_, fields_right + 22, 160, action_width, 38);

        auto field_row = [&](HWND left_label, HWND left_field, HWND right_label, HWND right_field, int y) {
            move(left_label, left_x, y, label_width, 34);
            move(left_field, left_x + label_width + 10, y, edit_width, 34);
            move(right_label, right_x, y, label_width, 34);
            move(right_field, right_x + label_width + 10, y, edit_width, 34);
        };
        field_row(main_label_, main_, contract_label_, contract_, 152);
        field_row(buffer_label_, buffer_, exchange_label_, exchange_, 198);
        field_row(start_label_, start_, end_label_, end_, 244);

        const int result_left = margin + 18;
        const int result_right = width - margin - 18;
        const int top = 378;
        const int kpi_width = 232;
        const int chart_left = result_left + kpi_width + 18;
        const int chart_width = result_right - chart_left;
        const int chart_height = std::clamp((height - top - 170) / 2 + 110, 210, 290);

        move(kpi_heading_, result_left, 346, kpi_width, 28);
        for (int i = 0; i < 4; ++i) {
            const int y = top + i * 54;
            move(kpi_names_[static_cast<std::size_t>(i)], result_left + 12, y, 90, 42);
            move(kpi_values_[static_cast<std::size_t>(i)], result_left + 102, y, kpi_width - 114, 42);
        }
        move(chart_.hwnd(), chart_left, 346, chart_width, chart_height + 32);
        ShowWindow(chart_.hwnd(), SW_SHOW);

        const int log_top = top + chart_height + 38;
        move(log_heading_, result_left, log_top - 30, result_right - result_left, 26);
        move(log_, result_left, log_top, result_right - result_left, std::max(72, height - 60 - log_top));
        move(status_, margin + 12, height - 42, std::max(200, width - margin * 2 - 250), 26);
        move(progress_, width - margin - 210, height - 38, 190, 18);
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }

    void draw_background(HDC dc) {
        RECT client{}; GetClientRect(hwnd_, &client);
        FillRect(dc, &client, background_brush_);
        rounded_card(dc, config_card_);
        rounded_card(dc, result_card_);

        const auto old_font = SelectObject(dc, section_font_);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kText);
        RECT config_title{config_card_.left + 16, config_card_.top + 8, config_card_.right - 16, config_card_.top + 34};
        DrawTextW(dc, L"配置面板", -1, &config_title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        auto separator = CreatePen(PS_SOLID, 1, kBorder);
        const auto old_pen = SelectObject(dc, separator);
        MoveToEx(dc, 24, client.bottom - 49, nullptr); LineTo(dc, client.right - 24, client.bottom - 49);
        SelectObject(dc, old_pen); DeleteObject(separator);
        SelectObject(dc, old_font);
    }

    void draw_button(const DRAWITEMSTRUCT& item) {
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const bool disabled = (item.itemState & ODS_DISABLED) != 0;
        const bool selected = (item.CtlID == ID_MODE_BOLL && mode_ == StrategyMode::Bollinger) ||
                              (item.CtlID == ID_MODE_MACD && mode_ == StrategyMode::Macd);
        const bool primary = item.CtlID == ID_RUN || selected;
        COLORREF fill_color = primary ? kPrimary : RGB(242, 245, 248);
        COLORREF border_color = primary ? kPrimary : kBorder;
        COLORREF text_color = primary ? RGB(255, 255, 255) : kText;
        if (pressed && primary) fill_color = kPrimaryHover;
        if (disabled) { fill_color = RGB(231, 235, 240); text_color = RGB(150, 158, 169); border_color = RGB(218, 223, 230); }

        auto brush = CreateSolidBrush(fill_color);
        auto pen = CreatePen(PS_SOLID, 1, border_color);
        const auto old_brush = SelectObject(item.hDC, brush);
        const auto old_pen = SelectObject(item.hDC, pen);
        RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, 8, 8);
        SelectObject(item.hDC, old_pen); SelectObject(item.hDC, old_brush);
        DeleteObject(pen); DeleteObject(brush);

        wchar_t text[128]{}; GetWindowTextW(item.hwndItem, text, 128);
        SetBkMode(item.hDC, TRANSPARENT); SetTextColor(item.hDC, text_color);
        const auto old_font = SelectObject(item.hDC, body_font_);
        RECT text_rect = item.rcItem;
        DrawTextW(item.hDC, text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(item.hDC, old_font);
    }

    void set_mode(StrategyMode mode) {
        mode_ = mode;
        InvalidateRect(boll_, nullptr, TRUE);
        InvalidateRect(macd_, nullptr, TRUE);
        set_text(status_, mode == StrategyMode::Bollinger
            ? L"BOLL：26 日均线 + 2 倍标准差；滑点缓冲会应用到成交价格。"
            : L"MACD：12/26 EMA + 9 DEA；滑点缓冲会应用到成交价格。");
    }

    std::string date_value(HWND picker) const {
        SYSTEMTIME value{};
        SendMessageW(picker, DTM_GETSYSTEMTIME, 0, reinterpret_cast<LPARAM>(&value));
        char date[16]{};
        sprintf_s(date, "%04u%02u%02u", value.wYear, value.wMonth, value.wDay);
        return date;
    }

    Query query() const {
        Query result;
        result.mode = mode_;
        result.main_code = text_utf8(main_);
        result.contract_code = text_utf8(contract_);
        result.buffer = text_utf8(buffer_);
        result.start_date = date_value(start_);
        result.end_date = date_value(end_);
        result.exchange = text_utf8(exchange_);
        return result;
    }

    void start_run() {
        if (running_) return;
        const Query request = query();
        if (request.main_code.empty() && request.contract_code.empty()) {
            MessageBoxW(hwnd_, L"请填写主连代码或期货代码。", L"参数不完整", MB_OK | MB_ICONINFORMATION);
            return;
        }
        running_ = true;
        started_at_ = std::chrono::steady_clock::now();
        EnableWindow(run_, FALSE); EnableWindow(export_, FALSE);
        ShowWindow(progress_, SW_SHOW); SendMessageW(progress_, PBM_SETMARQUEE, TRUE, 30);
        set_text(log_, L"正在连接行情数据源并执行回测...");
        set_text(status_, L"正在运行，请稍候...");

        const HWND target = hwnd_;
        BacktestService* service = &service_;
        std::thread([target, service, request] {
            auto payload = std::make_unique<ResultPayload>();
            try {
                payload->run = std::make_unique<BacktestRun>(service->run(request));
                payload->ok = true;
            } catch (const std::exception& error) {
                payload->error = error.what();
            }
            if (IsWindow(target)) PostMessageW(target, WM_BACKTEST_RESULT, 0, reinterpret_cast<LPARAM>(payload.release()));
        }).detach();
    }

    std::wstring build_log(const BacktestRun& run) const {
        std::wostringstream out;
        out << L"数据源: " << wide(run.result.provider) << L"    样本数: " << run.result.rows.size() << L"\r\n";
        out << L"------------------------------------------------------------\r\n";
        std::size_t events = 0;
        for (const auto& row : run.result.rows) {
            if (row.buy == 0 && row.sell == 0 && row.profit == 0) continue;
            out << wide(row.date) << L"  ";
            if (row.buy != 0) out << L"买入 @ " << number(row.buy) << L"  ";
            if (row.sell != 0) out << L"卖出 @ " << number(row.sell) << L"  ";
            if (row.profit != 0) out << L"盈亏 " << (row.profit > 0 ? L"+" : L"") << number(row.profit);
            out << L"\r\n";
            ++events;
        }
        if (events == 0) out << L"区间内没有产生完整交易。\r\n";
        return out.str();
    }

    void finish_run(ResultPayload* raw_payload) {
        std::unique_ptr<ResultPayload> payload(raw_payload);
        running_ = false;
        SendMessageW(progress_, PBM_SETMARQUEE, FALSE, 0); ShowWindow(progress_, SW_HIDE);
        EnableWindow(run_, TRUE);
        if (!payload->ok || !payload->run) {
            set_text(log_, wide(payload->error));
            set_text(status_, L"执行失败，请检查数据源、代码和网络配置。");
            MessageBoxW(hwnd_, wide(payload->error).c_str(), L"回测失败", MB_OK | MB_ICONERROR);
            return;
        }

        last_ = std::move(payload->run);
        EnableWindow(export_, TRUE);
        const auto& analytics = last_->analytics;
        set_text(kpi_values_[0], number(analytics.total_profit));
        set_text(kpi_values_[1], std::to_wstring(analytics.trade_count));
        set_text(kpi_values_[2], number(analytics.win_rate * 100.0, 1) + L"%");
        set_text(kpi_values_[3], number(analytics.max_drawdown));
        chart_.set_data(*last_);
        set_text(log_, build_log(*last_));

        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at_).count();
        std::wostringstream status;
        status << L"回测完成 · " << wide(last_->result.provider) << L" · " << last_->result.rows.size()
               << L" 条数据 · 耗时 " << std::fixed << std::setprecision(1) << elapsed << L"s";
        set_text(status_, status.str());
    }

    void export_result() {
        if (!last_) { MessageBoxW(hwnd_, L"请先完成一次回测。", L"提示", MB_OK | MB_ICONINFORMATION); return; }
        IFileSaveDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
        COMDLG_FILTERSPEC filter[] = {{L"CSV 文件", L"*.csv"}};
        dialog->SetFileTypes(1, filter); dialog->SetDefaultExtension(L"csv"); dialog->SetFileName(L"backtest_result.csv");
        if (SUCCEEDED(dialog->Show(hwnd_))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    std::string error;
                    if (!service_.export_csv_file(narrow(path), last_->result, error))
                        MessageBoxW(hwnd_, wide(error).c_str(), L"导出失败", MB_OK | MB_ICONERROR);
                    else set_text(status_, L"CSV 已导出。");
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    LRESULT handle(UINT message, WPARAM wp, LPARAM lp) {
        switch (message) {
        case WM_CREATE: create_controls(); return 0;
        case WM_SIZE: layout(LOWORD(lp), HIWORD(lp)); return 0;
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lp);
            limits->ptMinTrackSize = POINT{900, 700}; return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd_, &ps); draw_background(dc); EndPaint(hwnd_, &ps); return 0;
        }
        case WM_DRAWITEM: draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            const HWND control_hwnd = reinterpret_cast<HWND>(lp);
            const bool on_window_background = control_hwnd == title_ || control_hwnd == status_;
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, on_window_background ? kBackground : kCard);
            SetTextColor(dc, kText);
            return reinterpret_cast<LRESULT>(on_window_background ? background_brush_ : edit_brush_);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp); SetBkColor(dc, kCard); SetTextColor(dc, kText);
            return reinterpret_cast<LRESULT>(edit_brush_);
        }
        case WM_COMMAND:
            if (HIWORD(wp) == BN_CLICKED) {
                switch (LOWORD(wp)) {
                case ID_MODE_BOLL: set_mode(StrategyMode::Bollinger); return 0;
                case ID_MODE_MACD: set_mode(StrategyMode::Macd); return 0;
                case ID_RUN: start_run(); return 0;
                case ID_EXPORT: export_result(); return 0;
                default: break;
                }
            }
            break;
        case WM_BACKTEST_RESULT: finish_run(reinterpret_cast<ResultPayload*>(lp)); return 0;
        case WM_CLOSE:
            if (running_) { MessageBoxW(hwnd_, L"回测仍在运行，请等待任务完成后再关闭。", L"任务进行中", MB_OK | MB_ICONINFORMATION); return 0; }
            DestroyWindow(hwnd_); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: break;
        }
        return DefWindowProcW(hwnd_, message, wp, lp);
    }
};

struct LoginState {
    HINSTANCE instance{}; HWND window{}; HWND user{}; HWND password{}; HFONT font{}; HBRUSH brush{};
    std::string expected_user; std::string expected_password; bool finished = false; bool success = false;
};

LRESULT CALLBACK login_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<LoginState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<LoginState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state)); state->window = hwnd;
    }
    if (!state) return DefWindowProcW(hwnd, message, wp, lp);
    if (message == WM_CTLCOLORSTATIC) { SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT); return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH)); }
    if (message == WM_COMMAND && LOWORD(wp) == 3 && HIWORD(wp) == BN_CLICKED) {
        if (text_utf8(state->user) == state->expected_user && text_utf8(state->password) == state->expected_password) {
            state->success = true; state->finished = true; DestroyWindow(hwnd);
        } else MessageBoxW(hwnd, L"用户名或密码错误。", L"登录失败", MB_OK | MB_ICONWARNING);
        return 0;
    }
    if (message == WM_CLOSE) { state->finished = true; DestroyWindow(hwnd); return 0; }
    return DefWindowProcW(hwnd, message, wp, lp);
}

bool login(HINSTANCE instance) {
    LoginState state; state.instance = instance; state.font = make_font(14); state.brush = CreateSolidBrush(kBackground);
    state.expected_user = std::getenv("ARBITRAGE_USER") ? std::getenv("ARBITRAGE_USER") : "admin";
    state.expected_password = std::getenv("ARBITRAGE_PASSWORD") ? std::getenv("ARBITRAGE_PASSWORD") : "admin123";
    WNDCLASSW wc{}; wc.lpfnWndProc = login_proc; wc.hInstance = instance; wc.lpszClassName = L"ArbitrageTool.Login";
    wc.hbrBackground = state.brush; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc);
    state.window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW, wc.lpszClassName, L"登录 - 套利回测工具",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 460, 320, nullptr, nullptr, instance, &state);
    if (!state.window) return false;
    auto add = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
        HWND child = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style,
            x, y, w, h, state.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE); return child;
    };
    add(L"STATIC", L"套利回测工具", SS_CENTER, 40, 24, 360, 30, 0);
    add(L"STATIC", L"用户名", SS_CENTERIMAGE, 48, 82, 80, 34, 0);
    state.user = add(L"EDIT", L"admin", ES_AUTOHSCROLL | WS_TABSTOP, 135, 82, 255, 34, 1);
    add(L"STATIC", L"密码", SS_CENTERIMAGE, 48, 132, 80, 34, 0);
    state.password = add(L"EDIT", L"", ES_PASSWORD | ES_AUTOHSCROLL | WS_TABSTOP, 135, 132, 255, 34, 2);
    add(L"BUTTON", L"登录", BS_DEFPUSHBUTTON | WS_TABSTOP, 135, 188, 255, 40, 3);
    add(L"STATIC", L"默认账户：admin / admin123", SS_CENTER, 48, 244, 342, 24, 0);
    ShowWindow(state.window, SW_SHOWNORMAL); SetForegroundWindow(state.window); SetFocus(state.password);
    MSG message{};
    while (!state.finished && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(state.window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    DeleteObject(state.font); DeleteObject(state.brush);
    return state.success;
}
}

int run_win32_ui(void* instance, BacktestService& service) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_DATE_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    auto native_instance = static_cast<HINSTANCE>(instance);
#ifndef ARBITRAGE_UI_PREVIEW
    if (!login(native_instance)) return 0;
#endif
    MainWindow window(native_instance, service);
    return window.show();
}

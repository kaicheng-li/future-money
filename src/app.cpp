#include "app.h"
#include "data.h"
#include "strategy.h"

#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>

#include <memory>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <algorithm>

namespace {
enum : int { ID_MODE_BOLL = 100, ID_MODE_MACD, ID_MAIN, ID_CONTRACT, ID_BUFFER, ID_START, ID_END, ID_EXCHANGE, ID_RUN, ID_EXPORT, ID_RESULT, ID_STATUS, WM_RESULT = WM_APP + 1 };
HFONT font(int size, int weight = FW_NORMAL) { return CreateFontW(size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"); }
std::wstring ws(const std::string& value) { if (value.empty()) return {}; int n = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0); std::wstring out(static_cast<size_t>(n), L'\0'); MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), n); out.resize(static_cast<size_t>(n - 1)); return out; }
std::string narrow(const std::wstring& value) { if (value.empty()) return {}; int n = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string out(static_cast<size_t>(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), n, nullptr, nullptr); out.resize(static_cast<size_t>(n - 1)); return out; }
std::string utf8(HWND h) { int n = GetWindowTextLengthW(h); std::wstring value(n, L'\0'); GetWindowTextW(h, value.data(), n + 1); return narrow(value); }
void set_text(HWND h, const std::wstring& value) { SetWindowTextW(h, value.c_str()); }
struct Payload { bool ok; BacktestResult result; std::string error; };

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance) : instance_(instance) {}
    int show() {
        WNDCLASSW wc{}; wc.lpfnWndProc = &MainWindow::proc; wc.hInstance = instance_; wc.lpszClassName = L"ArbitrageTool.Main"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"套利回测工具", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 980, 720, nullptr, nullptr, instance_, this);
        if (!hwnd_) { MessageBoxW(nullptr, L"主窗口创建失败。", L"启动失败", MB_OK | MB_ICONERROR); return 1; }
        ShowWindow(hwnd_, SW_SHOWNORMAL); SetForegroundWindow(hwnd_); UpdateWindow(hwnd_);
        MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); } return static_cast<int>(msg.wParam);
    }
private:
    HINSTANCE instance_; HWND hwnd_{}; HWND title_{}, strategy_label_{}, main_label_{}, contract_label_{}, buffer_label_{}, exchange_label_{}, start_label_{}, end_label_{}, result_label_{}; HWND main_{}, contract_{}, buffer_{}, start_{}, end_{}, exchange_{}, result_{}, status_{}, run_{}, boll_{}, macd_{}, export_{}; StrategyMode mode_ = StrategyMode::Bollinger; std::unique_ptr<BacktestResult> last_;
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); if (msg == WM_NCCREATE) { self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = hwnd; } return self ? self->handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp); }
    HWND control(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) { HWND hWnd = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr); SendMessageW(hWnd, WM_SETFONT, reinterpret_cast<WPARAM>(font(18)), TRUE); return hWnd; }
    HWND label(const wchar_t* text, int x, int y) { return control(L"STATIC", text, 0, x, y, 100, 28, 0); }
    void create_controls() {
        title_ = control(L"STATIC", L"套利回测工具", SS_CENTER, 28, 20, 840, 42, 0); strategy_label_ = label(L"策略", 34, 86); boll_ = control(L"BUTTON", L"BOLL", BS_PUSHBUTTON, 145, 82, 100, 34, ID_MODE_BOLL); macd_ = control(L"BUTTON", L"MACD", BS_PUSHBUTTON, 255, 82, 100, 34, ID_MODE_MACD);
        main_label_ = label(L"主连代码", 34, 138); main_ = control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 145, 134, 210, 32, ID_MAIN); contract_label_ = label(L"期货代码", 390, 138); contract_ = control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 505, 134, 210, 32, ID_CONTRACT);
        buffer_label_ = label(L"指标参数", 34, 188); buffer_ = control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 145, 184, 210, 32, ID_BUFFER); exchange_label_ = label(L"交易所", 390, 188); exchange_ = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_BORDER, 505, 184, 210, 200, ID_EXCHANGE); for (const wchar_t* name : {L"郑商所", L"上期能源", L"广期所", L"中金所", L"大商所", L"上期所"}) SendMessageW(exchange_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name)); SendMessageW(exchange_, CB_SETCURSEL, 0, 0);
        start_label_ = label(L"开始日期", 34, 238); start_ = control(L"EDIT", L"20200101", WS_BORDER | ES_AUTOHSCROLL, 145, 234, 210, 32, ID_START); end_label_ = label(L"结束日期", 390, 238); end_ = control(L"EDIT", L"20201231", WS_BORDER | ES_AUTOHSCROLL, 505, 234, 210, 32, ID_END);
        run_ = control(L"BUTTON", L"开始回测", BS_DEFPUSHBUTTON, 735, 134, 130, 82, ID_RUN); export_ = control(L"BUTTON", L"导出 CSV", BS_PUSHBUTTON, 735, 226, 130, 40, ID_EXPORT); result_label_ = label(L"结果", 34, 295); result_ = control(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 34, 330, 900, 280, ID_RESULT); status_ = control(L"STATIC", L"就绪。主连代码优先使用 AkShare；留空则使用 Tushare。", 0, 34, 625, 900, 28, ID_STATUS); set_mode(StrategyMode::Bollinger);
    }
    void layout(int width, int height) {
        const int margin = 34, gap = 28, usable = std::max(420, width - margin * 2), col = (usable - gap) / 2, label_width = 100, edit_gap = 10, edit_width = std::max(120, col - label_width - edit_gap);
        const int left = margin, right = margin + col + gap;
        SetWindowPos(title_, nullptr, margin, 20, usable, 42, SWP_NOZORDER);
        SetWindowPos(strategy_label_, nullptr, left, 86, label_width, 28, SWP_NOZORDER); SetWindowPos(boll_, nullptr, left + label_width + edit_gap, 82, 100, 34, SWP_NOZORDER); SetWindowPos(macd_, nullptr, left + label_width + edit_gap + 110, 82, 100, 34, SWP_NOZORDER);
        SetWindowPos(main_label_, nullptr, left, 138, label_width, 28, SWP_NOZORDER); SetWindowPos(main_, nullptr, left + label_width + edit_gap, 134, edit_width, 32, SWP_NOZORDER);
        SetWindowPos(contract_label_, nullptr, right, 138, label_width, 28, SWP_NOZORDER); SetWindowPos(contract_, nullptr, right + label_width + edit_gap, 134, edit_width, 32, SWP_NOZORDER);
        SetWindowPos(buffer_label_, nullptr, left, 188, label_width, 28, SWP_NOZORDER); SetWindowPos(buffer_, nullptr, left + label_width + edit_gap, 184, edit_width, 32, SWP_NOZORDER);
        SetWindowPos(exchange_label_, nullptr, right, 188, label_width, 28, SWP_NOZORDER); SetWindowPos(exchange_, nullptr, right + label_width + edit_gap, 184, edit_width, 200, SWP_NOZORDER);
        SetWindowPos(start_label_, nullptr, left, 238, label_width, 28, SWP_NOZORDER); SetWindowPos(start_, nullptr, left + label_width + edit_gap, 234, edit_width, 32, SWP_NOZORDER);
        SetWindowPos(end_label_, nullptr, right, 238, label_width, 28, SWP_NOZORDER); SetWindowPos(end_, nullptr, right + label_width + edit_gap, 234, edit_width, 32, SWP_NOZORDER);
        SetWindowPos(run_, nullptr, margin, 278, 140, 38, SWP_NOZORDER); SetWindowPos(export_, nullptr, margin + 150, 278, 140, 38, SWP_NOZORDER);
        const int result_top = 360;
        const int status_height = 28;
        const int status_y = std::max(result_top + 100, height - 62);
        const int result_height = std::max(100, status_y - result_top - 16);
        SetWindowPos(result_label_, nullptr, margin, 325, 100, 28, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(result_, nullptr, margin, result_top, usable, result_height, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(status_, nullptr, margin, status_y, usable, status_height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void set_mode(StrategyMode mode) { mode_ = mode; EnableWindow(contract_, mode == StrategyMode::Bollinger ? TRUE : TRUE); set_text(status_, mode == StrategyMode::Bollinger ? L"BOLL：26 日均线 + 2 倍标准差" : L"MACD：12/26 EMA + 9 DEA"); }
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_CREATE) { create_controls(); return 0; }
        if (msg == WM_GETMINMAXINFO) { auto* limits = reinterpret_cast<MINMAXINFO*>(lp); limits->ptMinTrackSize.x = 760; limits->ptMinTrackSize.y = 620; return 0; }
        if (msg == WM_SIZE) { layout(LOWORD(lp), HIWORD(lp)); return 0; }
        if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED) { switch (LOWORD(wp)) { case ID_MODE_BOLL: set_mode(StrategyMode::Bollinger); break; case ID_MODE_MACD: set_mode(StrategyMode::Macd); break; case ID_RUN: start_run(); break; case ID_EXPORT: export_result(); break; } return 0; }
        if (msg == WM_RESULT) { std::unique_ptr<Payload> payload(reinterpret_cast<Payload*>(lp)); EnableWindow(run_, TRUE); if (!payload->ok) { set_text(result_, ws(payload->error)); set_text(status_, L"执行失败"); return 0; } last_ = std::make_unique<BacktestResult>(std::move(payload->result)); std::wostringstream out; out << L"数据源：" << ws(last_->provider) << L"\r\n总收益：" << last_->total_profit << L"\r\n样本数：" << last_->rows.size() << L"\r\n\r\n"; for (size_t i = last_->rows.size() > 12 ? last_->rows.size() - 12 : 0; i < last_->rows.size(); ++i) out << ws(last_->rows[i].date) << L"  close=" << last_->rows[i].close << L"  state=" << last_->rows[i].state << L"  profit=" << last_->rows[i].profit << L"\r\n"; set_text(result_, out.str()); set_text(status_, L"计算完成，可以导出 CSV"); return 0; }
        if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; } return DefWindowProcW(hwnd_, msg, wp, lp);
    }
    Query query() const { Query q; q.mode = mode_; q.main_code = utf8(main_); q.contract_code = utf8(contract_); q.buffer = utf8(buffer_); q.start_date = utf8(start_); q.end_date = utf8(end_); wchar_t exchange[64]{}; GetWindowTextW(exchange_, exchange, 64); q.exchange = narrow(exchange); return q; }
    void start_run() { Query q = query(); EnableWindow(run_, FALSE); set_text(status_, L"正在请求行情数据，请稍候..."); std::thread([this, q] { auto payload = std::make_unique<Payload>(); try { DataProvider provider; auto rows = provider.fetch(q); payload->result = run_backtest(std::move(rows), q.mode, q.buffer, q.main_code.empty() ? "Tushare" : "AkShare"); payload->ok = true; } catch (const std::exception& e) { payload->ok = false; payload->error = e.what(); } PostMessageW(hwnd_, WM_RESULT, 0, reinterpret_cast<LPARAM>(payload.release())); }).detach(); }
    void export_result() { if (!last_) { MessageBoxW(hwnd_, L"请先完成一次回测。", L"提示", MB_OK | MB_ICONINFORMATION); return; } IFileSaveDialog* dialog = nullptr; if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return; COMDLG_FILTERSPEC filter[] = {{L"CSV 文件", L"*.csv"}}; dialog->SetFileTypes(1, filter); dialog->SetDefaultExtension(L"csv"); if (SUCCEEDED(dialog->Show(hwnd_))) { IShellItem* item = nullptr; if (SUCCEEDED(dialog->GetResult(&item))) { PWSTR path = nullptr; if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { std::string error; if (!export_csv(narrow(path), *last_, error)) MessageBoxW(hwnd_, ws(error).c_str(), L"导出失败", MB_OK | MB_ICONERROR); CoTaskMemFree(path); } item->Release(); } } dialog->Release(); }
};

struct LoginState {
    HINSTANCE instance{};
    HWND window{};
    HWND user{};
    HWND pass{};
    std::string expected_user;
    std::string expected_password;
    bool finished = false;
    bool success = false;
};

LRESULT CALLBACK login_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<LoginState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        state = static_cast<LoginState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->window = hwnd;
    }
    if (!state) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_COMMAND && LOWORD(wp) == 3 && HIWORD(wp) == BN_CLICKED) {
        char user[128]{}, password[128]{};
        GetWindowTextA(state->user, user, sizeof(user));
        GetWindowTextA(state->pass, password, sizeof(password));
        if (state->expected_user == user && state->expected_password == password) {
            state->success = true;
            state->finished = true;
            DestroyWindow(hwnd);
        } else {
            MessageBoxW(hwnd, L"用户名或密码错误。", L"登录失败", MB_OK | MB_ICONWARNING);
        }
        return 0;
    }
    if (msg == WM_CLOSE) {
        state->finished = true;
        state->success = false;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool login(HINSTANCE instance) {
    LoginState state;
    state.instance = instance;
    state.expected_user = std::getenv("ARBITRAGE_USER") ? std::getenv("ARBITRAGE_USER") : "admin";
    state.expected_password = std::getenv("ARBITRAGE_PASSWORD") ? std::getenv("ARBITRAGE_PASSWORD") : "admin123";
    WNDCLASSW wc{}; wc.lpfnWndProc = &login_proc; wc.hInstance = instance; wc.lpszClassName = L"ArbitrageTool.Login"; wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    state.window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW, wc.lpszClassName, L"登录 - 套利回测工具", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 430, 300, nullptr, nullptr, instance, &state);
    if (!state.window) { MessageBoxW(nullptr, L"登录窗口创建失败。", L"启动失败", MB_OK | MB_ICONERROR); return false; }
    state.user = CreateWindowW(L"EDIT", L"admin", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 145, 55, 220, 32, state.window, reinterpret_cast<HMENU>(1), instance, nullptr);
    state.pass = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL, 145, 105, 220, 32, state.window, reinterpret_cast<HMENU>(2), instance, nullptr);
    CreateWindowW(L"STATIC", L"用户名", WS_CHILD | WS_VISIBLE, 55, 60, 80, 24, state.window, nullptr, instance, nullptr);
    CreateWindowW(L"STATIC", L"密码", WS_CHILD | WS_VISIBLE, 55, 110, 80, 24, state.window, nullptr, instance, nullptr);
    CreateWindowW(L"BUTTON", L"登录", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 145, 165, 220, 38, state.window, reinterpret_cast<HMENU>(3), instance, nullptr);
    CreateWindowW(L"STATIC", L"默认账户：admin / admin123（可用环境变量覆盖）", WS_CHILD | WS_VISIBLE, 55, 220, 320, 24, state.window, nullptr, instance, nullptr);
    ShowWindow(state.window, SW_SHOWNORMAL); SetForegroundWindow(state.window); SetFocus(state.pass);
    MSG msg{};
    while (!state.finished && GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return state.success;
}
}

int run_application(void* instance) { auto h = static_cast<HINSTANCE>(instance); if (!login(h)) return 0; CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); MainWindow window(h); const int result = window.show(); CoUninitialize(); return result; }

#include "app.h"

#include "backtest_service.h"
#include "win32_ui.h"

#include <windows.h>
#include <objbase.h>

int run_application(void* instance) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    BacktestService service;
    const int result = run_win32_ui(instance, service);
    CoUninitialize();
    return result;
}

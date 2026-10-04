#include "backtest_service.h"

#include "data.h"
#include "strategy.h"

#include <utility>

BacktestRun BacktestService::run(const Query& query) const {
    DataProvider provider;
    auto rows = provider.fetch(query);
    auto result = run_backtest(
        std::move(rows),
        query.mode,
        query.buffer,
        query.main_code.empty() ? "Tushare" : "AkShare");
    auto analytics = analyze_backtest(result);
    return BacktestRun{std::move(result), std::move(analytics)};
}

bool BacktestService::export_csv_file(const std::string& path, const BacktestResult& result, std::string& error) const {
    return export_csv(path, result, error);
}

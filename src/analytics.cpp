#include "analytics.h"

#include <algorithm>

BacktestAnalytics analyze_backtest(const BacktestResult& result) {
    BacktestAnalytics analytics;
    analytics.equity_curve.reserve(result.rows.size());
    analytics.drawdown_curve.reserve(result.rows.size());

    double equity = 0;
    double peak = 0;
    std::size_t wins = 0;

    for (const auto& row : result.rows) {
        equity += row.profit;
        peak = std::max(peak, equity);
        const double drawdown = equity - peak;
        analytics.equity_curve.push_back(equity);
        analytics.drawdown_curve.push_back(drawdown);
        analytics.max_drawdown = std::min(analytics.max_drawdown, drawdown);
        if (row.profit != 0) {
            ++analytics.trade_count;
            if (row.profit > 0) ++wins;
        }
    }

    analytics.total_profit = equity;
    if (analytics.trade_count != 0) {
        analytics.win_rate = static_cast<double>(wins) / static_cast<double>(analytics.trade_count);
    }
    return analytics;
}

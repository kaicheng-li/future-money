#pragma once

#include "model.h"

#include <cstddef>
#include <vector>

struct BacktestAnalytics {
    double total_profit = 0;
    std::size_t trade_count = 0;
    double win_rate = 0;
    double max_drawdown = 0;
    std::vector<double> equity_curve;
    std::vector<double> drawdown_curve;
};

BacktestAnalytics analyze_backtest(const BacktestResult& result);

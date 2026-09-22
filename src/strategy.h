#pragma once

#include "model.h"

BacktestResult run_backtest(std::vector<Candle> rows, StrategyMode mode, const std::string& buffer, const std::string& provider);

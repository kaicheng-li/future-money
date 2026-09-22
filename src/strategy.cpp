#include "strategy.h"

#include <cmath>
#include <numeric>
#include <stdexcept>

namespace {
double parse_buffer(const std::string& text) { if (text.empty()) return 0; try { return std::stod(text); } catch (...) { throw std::runtime_error("指标参数必须是数字"); } }
double ema_next(double previous, double price, int span, bool first) { const double alpha = 2.0 / (span + 1); return first ? price : previous + alpha * (price - previous); }
void settle_profit(std::vector<Candle>& rows, double& total) {
    int position = 0; double entry = 0;
    for (auto& c : rows) {
        c.profit = 0;
        if (position == 0) {
            if (c.buy != 0) { position = 1; entry = c.buy; }
            else if (c.sell != 0) { position = -1; entry = c.sell; }
            continue;
        }
        if (position > 0 && c.sell != 0) {
            c.profit = c.sell - entry; total += c.profit;
            if (c.state < 0) { position = -1; entry = c.sell; } else { position = 0; entry = 0; }
        } else if (position < 0 && c.buy != 0) {
            c.profit = entry - c.buy; total += c.profit;
            if (c.state > 0) { position = 1; entry = c.buy; } else { position = 0; entry = 0; }
        }
    }
}
}

BacktestResult run_backtest(std::vector<Candle> rows, StrategyMode mode, const std::string& buffer, const std::string& provider) {
    if (rows.empty()) throw std::runtime_error("没有获取到行情数据"); const double extra = parse_buffer(buffer);
    if (mode == StrategyMode::Bollinger) {
        for (size_t i = 0; i < rows.size(); ++i) { if (i + 1 >= 26) { double sum = 0; for (size_t j = i + 1 - 26; j <= i; ++j) sum += rows[j].close; rows[i].mid = sum / 26; double variance = 0; for (size_t j = i + 1 - 26; j <= i; ++j) variance += std::pow(rows[j].close - rows[i].mid, 2); const double sd = std::sqrt(variance / 25); rows[i].top = rows[i].mid + 2 * sd; rows[i].bottom = rows[i].mid - 2 * sd; } if (rows[i].close > rows[i].top && rows[i].top != 0) rows[i].state = 1; else if (rows[i].close < rows[i].bottom && rows[i].bottom != 0) rows[i].state = -1; }
        for (size_t i = 1; i < rows.size(); ++i) { if (rows[i].state > 0 && rows[i - 1].state <= 0) rows[i].buy = rows[i].top + extra; if (rows[i].state < 0 && rows[i - 1].state >= 0) rows[i].sell = rows[i].bottom - extra; if (rows[i].state == 0 && rows[i - 1].state > 0) rows[i].sell = rows[i].mid - extra; if (rows[i].state == 0 && rows[i - 1].state < 0) rows[i].buy = rows[i].mid + extra; }
    } else {
        double e12 = 0, e26 = 0, dea = 0; for (size_t i = 0; i < rows.size(); ++i) { const bool first = i == 0; e12 = ema_next(e12, rows[i].close, 12, first); e26 = ema_next(e26, rows[i].close, 26, first); rows[i].diff = std::round((e12 - e26) * 100.0) / 100.0; dea = ema_next(dea, rows[i].diff, 9, first); rows[i].dea = std::round(dea * 100.0) / 100.0; rows[i].macd = 2 * (rows[i].diff - rows[i].dea); rows[i].state = rows[i].macd > 0 ? 1 : (rows[i].macd < 0 ? -1 : 0); }
        for (size_t i = 1; i < rows.size(); ++i) { if (rows[i].state > 0 && rows[i - 1].state <= 0) rows[i].buy = rows[i].close + extra; if (rows[i].state < 0 && rows[i - 1].state >= 0) rows[i].sell = rows[i].close - extra; }
    }
    double total = 0; settle_profit(rows, total); return BacktestResult{std::move(rows), total, provider, "计算完成"};
}

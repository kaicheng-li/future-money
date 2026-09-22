#pragma once

#include <string>
#include <vector>

enum class StrategyMode { Bollinger, Macd };

struct Query {
    StrategyMode mode = StrategyMode::Bollinger;
    std::string main_code;
    std::string contract_code;
    std::string buffer;
    std::string start_date;
    std::string end_date;
    std::string exchange;
};

struct Candle {
    std::string date;
    double open = 0;
    double high = 0;
    double low = 0;
    double close = 0;
    double volume = 0;
    double open_interest = 0;
    double settle = 0;
    double mid = 0;
    double top = 0;
    double bottom = 0;
    double diff = 0;
    double dea = 0;
    double macd = 0;
    int state = 0;
    double buy = 0;
    double sell = 0;
    double profit = 0;
};

struct BacktestResult {
    std::vector<Candle> rows;
    double total_profit = 0;
    std::string provider;
    std::string message;
};

#pragma once

#include "model.h"

#include <string>
#include <vector>

class DataProvider {
public:
    std::vector<Candle> fetch(const Query& query) const;
};

std::string csv_escape(const std::string& value);
bool export_csv(const std::string& path, const BacktestResult& result, std::string& error);

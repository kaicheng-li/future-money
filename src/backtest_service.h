#pragma once

#include "analytics.h"
#include "model.h"

#include <string>

struct BacktestRun {
    BacktestResult result;
    BacktestAnalytics analytics;
};

class BacktestService {
public:
    BacktestRun run(const Query& query) const;
    bool export_csv_file(const std::string& path, const BacktestResult& result, std::string& error) const;
};

#include "data.h"
#include "json.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <ctime>
#include <cctype>

namespace {
std::string env(const char* name) { const char* value = std::getenv(name); return value ? value : ""; }

std::wstring widen_ascii(const std::string& value) { return std::wstring(value.begin(), value.end()); }

std::string http_request(const std::wstring& url, const std::string& body = {}) {
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts); wchar_t host[256]{}; wchar_t path[2048]{}; wchar_t extra[2048]{};
    parts.lpszHostName = host; parts.dwHostNameLength = 255; parts.lpszUrlPath = path; parts.dwUrlPathLength = 2047; parts.lpszExtraInfo = extra; parts.dwExtraInfoLength = 2047;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) throw std::runtime_error("invalid request URL");
    HINTERNET session = WinHttpOpen(L"ArbitrageTool/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!session) throw std::runtime_error("WinHttpOpen failed");
    HINTERNET connect = WinHttpConnect(session, host, parts.nPort, 0);
    std::wstring target = std::wstring(path) + extra; HINTERNET request = connect ? WinHttpOpenRequest(connect, body.empty() ? L"GET" : L"POST", target.c_str(), nullptr, nullptr, nullptr, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (!request) { if (connect) WinHttpCloseHandle(connect); WinHttpCloseHandle(session); throw std::runtime_error("cannot open HTTP request"); }
    BOOL ok;
    if (body.empty()) ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    else { const wchar_t* headers = L"Content-Type: application/json\r\n"; ok = WinHttpSendRequest(request, headers, static_cast<DWORD>(-1), (LPVOID)body.data(), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0); }
    if (ok) ok = WinHttpReceiveResponse(request, nullptr);
    std::string result;
    if (ok) {
        DWORD size = 0;
        do { if (!WinHttpQueryDataAvailable(request, &size) || !size) break; std::string chunk(size, '\0'); DWORD read = 0; if (!WinHttpReadData(request, chunk.data(), size, &read)) break; result.append(chunk.data(), read); } while (size);
    }
    WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
    if (!ok) throw std::runtime_error("HTTP request failed");
    if (result.empty()) throw std::runtime_error("HTTP response was empty");
    return result;
}

double number(const Json& value) {
    if (value.is_number()) return value.as_number();
    if (value.is_string()) { try { return std::stod(value.as_string()); } catch (...) {} }
    return 0;
}

std::string string_value(const Json& value) { return value.is_string() ? value.as_string() : (value.is_number() ? std::to_string(value.as_number()) : ""); }

std::vector<Candle> parse_sina(const std::string& text) {
    const auto left = text.find('['); const auto right = text.rfind(']'); if (left == std::string::npos || right <= left) throw std::runtime_error("Sina returned empty data");
    Json root = Json::parse(text.substr(left, right - left + 1)); std::vector<Candle> rows;
    for (const auto& item : root.as_array()) {
        Candle c;
        if (item.is_array()) {
            if (item.as_array().size() < 8) continue;
            const auto& v = item.as_array(); c.date = string_value(v[0]); c.open = number(v[1]); c.high = number(v[2]); c.low = number(v[3]); c.close = number(v[4]); c.volume = number(v[5]); c.open_interest = number(v[6]); c.settle = number(v[7]);
        } else if (item.is_object()) {
            // Current Sina JSONP format uses d/o/h/l/c/v/p/s keys.
            const auto& v = item.as_object();
            auto field = [&](const char* key) -> const Json* { auto it = v.find(key); return it == v.end() ? nullptr : &it->second; };
            const Json* date = field("d"); const Json* open = field("o"); const Json* high = field("h"); const Json* low = field("l"); const Json* close = field("c"); const Json* volume = field("v"); const Json* position = field("p"); const Json* settle = field("s");
            if (!date || !open || !high || !low || !close) continue;
            c.date = string_value(*date); c.open = number(*open); c.high = number(*high); c.low = number(*low); c.close = number(*close); c.volume = volume ? number(*volume) : 0; c.open_interest = position ? number(*position) : 0; c.settle = settle ? number(*settle) : 0;
        } else continue;
        if (!c.date.empty()) rows.push_back(c);
    }
    if (rows.empty()) throw std::runtime_error("Sina returned no usable candle records");
    return rows;
}

std::vector<Candle> parse_tushare(const std::string& text) {
    Json root = Json::parse(text); const auto& data = root.at("data"); const auto& fields = data.at("fields").as_array(); const auto& items = data.at("items").as_array();
    std::map<std::string, size_t> index; for (size_t i = 0; i < fields.size(); ++i) index[string_value(fields[i])] = i;
    auto col = [&](const Json::array& row, const char* key) { auto it = index.find(key); return it == index.end() || it->second >= row.size() ? Json(nullptr) : row[it->second]; };
    std::vector<Candle> rows; for (const auto& item : items) { const auto& v = item.as_array(); Candle c; c.date = string_value(col(v, "trade_date")); c.open = number(col(v, "open")); c.high = number(col(v, "high")); c.low = number(col(v, "low")); c.close = number(col(v, "close")); c.volume = number(col(v, "vol")); c.open_interest = number(col(v, "oi")); c.settle = number(col(v, "settle")); rows.push_back(c); } return rows;
}

std::string date_before(const std::string& date) {
    std::tm tm{}; std::istringstream in(date); in >> std::get_time(&tm, "%Y%m%d"); if (in.fail()) throw std::runtime_error("日期格式应为 YYYYMMDD");
    std::time_t t = std::mktime(&tm); t -= 100 * 24 * 60 * 60; tm = *std::localtime(&t); std::ostringstream out; out << std::put_time(&tm, "%Y%m%d"); return out.str();
}

void normalize(std::vector<Candle>& rows) { std::sort(rows.begin(), rows.end(), [](const Candle& a, const Candle& b) { return a.date < b.date; }); rows.erase(std::unique(rows.begin(), rows.end(), [](const Candle& a, const Candle& b) { return a.date == b.date; }), rows.end()); }
}

std::vector<Candle> DataProvider::fetch(const Query& q) const {
    if (q.start_date.empty() || q.end_date.empty()) throw std::runtime_error("请填写起止日期");
    const std::string before = date_before(q.start_date); std::vector<Candle> rows;
    if (!q.main_code.empty()) {
        std::string symbol = q.main_code;
        std::transform(symbol.begin(), symbol.end(), symbol.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        // Accept both a product code (RB -> RB0) and an explicit continuous or
        // dated contract (RB0/RB2410) as entered by the original program.
        const bool has_digit = std::any_of(symbol.begin(), symbol.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
        if (!has_digit) symbol += '0';
        // Sina returns the complete history for a continuous symbol; filter it below.
        const std::string url = "https://stock2.finance.sina.com.cn/futures/api/jsonp.php/var=/InnerFuturesNewService.getDailyKLine?symbol=" + symbol;
        try {
            rows = parse_sina(http_request(widen_ascii(url)));
        } catch (const std::exception&) {
            // Keep compatibility with the older JSONP route used by the original build.
            const std::string legacy = "https://stock2.finance.sina.com.cn/futures/api/jsonp.php//InnerFuturesNewService.getDailyKLine?symbol=" + symbol;
            rows = parse_sina(http_request(widen_ascii(legacy)));
        }
    } else {
        const std::string token = env("TUSHARE_TOKEN"); if (token.empty()) throw std::runtime_error("未配置 TUSHARE_TOKEN；如使用主连代码可绕过 Tushare");
        static const std::map<std::string, std::string> suffix{{"中金所", ".CFX"}, {"上期所", ".SHF"}, {"大商所", ".DCE"}, {"郑商所", ".ZCE"}, {"上期能源", ".INE"}, {"广期所", ".GFE"}};
        const auto it = suffix.find(q.exchange); if (it == suffix.end()) throw std::runtime_error("请选择有效交易所"); const std::string code = q.contract_code + it->second;
        const auto request = [&](const std::string& start, const std::string& end) { std::ostringstream body; body << "{\"api_name\":\"fut_daily\",\"token\":\"" << token << "\",\"params\":{\"ts_code\":\"" << code << "\",\"start_date\":\"" << start << "\",\"end_date\":\"" << end << "\"},\"fields\":\"\"}"; return parse_tushare(http_request(L"https://api.tushare.pro", body.str())); };
        auto before_rows = request(before, q.start_date); auto current_rows = request(q.start_date, q.end_date); rows.insert(rows.end(), before_rows.begin(), before_rows.end()); rows.insert(rows.end(), current_rows.begin(), current_rows.end());
    }
    normalize(rows);
    auto key = [](std::string value) { value.erase(std::remove(value.begin(), value.end(), '-'), value.end()); return value.substr(0, 8); };
    const std::string min_date = key(before), max_date = key(q.end_date); rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Candle& row) { const auto d = key(row.date); return d < min_date || d > max_date; }), rows.end());
    return rows;
}

std::string csv_escape(const std::string& value) { if (value.find_first_of(",\"\n") == std::string::npos) return value; return "\"" + value + "\""; }

bool export_csv(const std::string& path, const BacktestResult& result, std::string& error) {
    std::ofstream out(path, std::ios::binary); if (!out) { error = "无法写入 CSV 文件"; return false; }
    out << "date,open,high,low,close,volume,open_interest,settle,mid,top,bottom,diff,dea,macd,state,buy,sell,profit\n";
    out << std::setprecision(12); for (const auto& c : result.rows) out << csv_escape(c.date) << ',' << c.open << ',' << c.high << ',' << c.low << ',' << c.close << ',' << c.volume << ',' << c.open_interest << ',' << c.settle << ',' << c.mid << ',' << c.top << ',' << c.bottom << ',' << c.diff << ',' << c.dea << ',' << c.macd << ',' << c.state << ',' << c.buy << ',' << c.sell << ',' << c.profit << "\n";
    return true;
}

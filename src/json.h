#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

class Json {
public:
    using object = std::map<std::string, Json>;
    using array = std::vector<Json>;
    using value = std::variant<std::nullptr_t, bool, double, std::string, array, object>;

    Json() : data_(nullptr) {}
    Json(std::nullptr_t) : data_(nullptr) {}
    Json(bool v) : data_(v) {}
    Json(double v) : data_(v) {}
    Json(std::string v) : data_(std::move(v)) {}
    Json(array v) : data_(std::move(v)) {}
    Json(object v) : data_(std::move(v)) {}

    static Json parse(const std::string& text);
    bool is_null() const { return std::holds_alternative<std::nullptr_t>(data_); }
    bool is_array() const { return std::holds_alternative<array>(data_); }
    bool is_object() const { return std::holds_alternative<object>(data_); }
    bool is_string() const { return std::holds_alternative<std::string>(data_); }
    bool is_number() const { return std::holds_alternative<double>(data_); }
    const array& as_array() const { return std::get<array>(data_); }
    const object& as_object() const { return std::get<object>(data_); }
    const std::string& as_string() const { return std::get<std::string>(data_); }
    double as_number() const { return std::get<double>(data_); }
    const Json& at(const std::string& key) const { return as_object().at(key); }

private:
    value data_;
};

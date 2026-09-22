#include "json.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace {
class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}
    Json parse() {
        skip();
        Json value = parse_value();
        skip();
        if (pos_ != text_.size()) throw std::runtime_error("unexpected JSON characters");
        return value;
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    void skip() { while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
    char take() { if (pos_ >= text_.size()) throw std::runtime_error("unexpected end of JSON"); return text_[pos_++]; }
    void expect(char c) { if (take() != c) throw std::runtime_error("invalid JSON punctuation"); }

    Json parse_value() {
        skip();
        if (pos_ >= text_.size()) throw std::runtime_error("missing JSON value");
        switch (text_[pos_]) {
        case '{': return parse_object();
        case '[': return parse_array();
        case '"': return Json(parse_string());
        case 't': return literal("true", Json(true));
        case 'f': return literal("false", Json(false));
        case 'n': return literal("null", Json(nullptr));
        default: return parse_number();
        }
    }

    Json literal(const char* word, Json result) {
        const size_t n = std::strlen(word);
        if (text_.compare(pos_, n, word) != 0) throw std::runtime_error("invalid JSON literal");
        pos_ += n;
        return result;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (pos_ < text_.size()) {
            char c = take();
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            char escaped = take();
            switch (escaped) {
            case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
            case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
            case 'r': out += '\r'; break; case 't': out += '\t'; break;
            default: throw std::runtime_error("unsupported JSON escape");
            }
        }
        throw std::runtime_error("unterminated JSON string");
    }

    Json parse_number() {
        const size_t start = pos_;
        if (text_[pos_] == '-') ++pos_;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        if (pos_ < text_.size() && text_[pos_] == '.') { ++pos_; while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_; if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        char* end = nullptr;
        const double number = std::strtod(text_.c_str() + start, &end);
        if (end != text_.c_str() + pos_) throw std::runtime_error("invalid JSON number");
        return Json(number);
    }

    Json parse_array() {
        expect('['); Json::array values; skip();
        if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return Json(std::move(values)); }
        while (true) {
            values.push_back(parse_value()); skip();
            if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return Json(std::move(values)); }
            expect(',');
        }
    }

    Json parse_object() {
        expect('{'); Json::object values; skip();
        if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return Json(std::move(values)); }
        while (true) {
            skip(); std::string key = parse_string(); skip(); expect(':'); values.emplace(std::move(key), parse_value()); skip();
            if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return Json(std::move(values)); }
            expect(',');
        }
    }
};
}

Json Json::parse(const std::string& text) { return Parser(text).parse(); }

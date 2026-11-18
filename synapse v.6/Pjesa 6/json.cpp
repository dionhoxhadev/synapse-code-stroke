#include "json.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>

Json Json::of(bool value)
{
    Json j;
    j.type = Bool;
    j.boolean = value;
    return j;
}

Json Json::decimal(double value)
{
    Json j;
    j.type = Number;
    j.number = value;
    j.oneDecimal = true;
    return j;
}

Json Json::of(int value)
{
    Json j;
    j.type = Number;
    j.number = value;
    return j;
}

Json Json::of(const std::string& value)
{
    Json j;
    j.type = Text;
    j.text = value;
    return j;
}

Json Json::of(const std::vector<std::string>& values)
{
    Json j = array();
    for (const auto& v : values) j.add(of(v));
    return j;
}

Json Json::array()
{
    Json j;
    j.type = Array;
    return j;
}

Json Json::object()
{
    Json j;
    j.type = Object;
    return j;
}

void Json::add(const Json& item)
{
    items.push_back(item);
}

void Json::set(const std::string& key, const Json& value)
{
    keys.push_back(key);
    items.push_back(value);
}

const Json* Json::get(const std::string& key) const
{
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key) return &items[i];
    return nullptr;
}

static std::string escaped(const std::string& s)
{
    static const char* HEX = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) { out += "\\u00"; out += HEX[c >> 4]; out += HEX[c & 15]; }
        else out += (char)c;
    }
    return out + "\"";
}

std::string Json::dump(int depth) const
{
    std::string inner((depth + 1) * 2, ' '), outer(depth * 2, ' ');
    char buffer[32];
    switch (type) {
    case Null:   return "null";
    case Bool:   return boolean ? "true" : "false";
    case Number:
        if (!oneDecimal) return std::to_string((long)number);
        std::snprintf(buffer, sizeof buffer, "%.1f", number);
        return buffer;
    case Text:   return escaped(text);
    default:     break;
    }

    bool isObject = type == Object;
    if (items.empty()) return isObject ? "{}" : "[]";
    std::string out = isObject ? "{\n" : "[\n";
    for (size_t i = 0; i < items.size(); ++i) {
        out += inner;
        if (isObject) out += escaped(keys[i]) + ": ";
        out += items[i].dump(depth + 1);
        out += i + 1 < items.size() ? ",\n" : "\n";
    }
    return out + outer + (isObject ? "}" : "]");
}

namespace {

class Parser
{
public:
    explicit Parser(const std::string& source) : s_(source) {}

    bool parseAll(Json& out)
    {
        if (!parseValue(out)) return false;
        skipSpace();
        return pos_ == s_.size();
    }

private:
    void skipSpace()
    {
        while (pos_ < s_.size() && std::isspace((unsigned char)s_[pos_])) ++pos_;
    }

    bool accept(char c)
    {
        skipSpace();
        if (pos_ >= s_.size() || s_[pos_] != c) return false;
        ++pos_;
        return true;
    }

    bool acceptWord(const std::string& word)
    {
        if (s_.compare(pos_, word.size(), word) != 0) return false;
        pos_ += word.size();
        return true;
    }

    bool parseValue(Json& out)
    {
        skipSpace();
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') {
            out = Json::of(std::string());
            return parseText(out.text);
        }
        if (acceptWord("true")) { out = Json::of(true); return true; }
        if (acceptWord("false")) { out = Json::of(false); return true; }
        if (acceptWord("null")) { out = Json(); return true; }
        return parseNumber(out);
    }

    bool parseNumber(Json& out)
    {
        const char* start = s_.c_str() + pos_;
        if (*start != '-' && !std::isdigit((unsigned char)*start)) return false;
        char* end = nullptr;
        double value = std::strtod(start, &end);
        if (end == start) return false;
        pos_ += end - start;
        out = Json::of(0);
        out.number = value;
        return true;
    }

    bool parseText(std::string& out)
    {
        ++pos_;   // opening quote
        while (pos_ < s_.size() && s_[pos_] != '"') {
            char c = s_[pos_++];
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) return false;
            char e = s_[pos_++];
            if (e == 'n') out += '\n';
            else if (e == 'r') out += '\r';
            else if (e == 't') out += '\t';
            else if (e == 'b') out += '\b';
            else if (e == 'f') out += '\f';
            else if (e == 'u') { if (!parseUnicode(out)) return false; }
            else out += e;
        }
        if (pos_ >= s_.size()) return false;
        ++pos_;   // closing quote
        return true;
    }

    bool parseUnicode(std::string& out)
    {
        if (pos_ + 4 > s_.size()) return false;
        int code = (int)std::strtol(s_.substr(pos_, 4).c_str(), nullptr, 16);
        pos_ += 4;
        if (code < 0x80) {
            out += (char)code;
        } else if (code < 0x800) {
            out += (char)(0xC0 | (code >> 6));
            out += (char)(0x80 | (code & 63));
        } else {
            out += (char)(0xE0 | (code >> 12));
            out += (char)(0x80 | ((code >> 6) & 63));
            out += (char)(0x80 | (code & 63));
        }
        return true;
    }

    bool parseArray(Json& out)
    {
        out = Json::array();
        ++pos_;
        if (accept(']')) return true;
        do {
            Json item;
            if (!parseValue(item)) return false;
            out.add(item);
        } while (accept(','));
        return accept(']');
    }

    bool parseObject(Json& out)
    {
        out = Json::object();
        ++pos_;
        if (accept('}')) return true;
        do {
            skipSpace();
            std::string key;
            if (pos_ >= s_.size() || s_[pos_] != '"' || !parseText(key)) return false;
            if (!accept(':')) return false;
            Json value;
            if (!parseValue(value)) return false;
            out.set(key, value);
        } while (accept(','));
        return accept('}');
    }

    const std::string& s_;
    size_t pos_ = 0;
};

} // namespace

bool Json::parse(const std::string& source, Json& out)
{
    return Parser(source).parseAll(out);
}

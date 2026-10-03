
#pragma once

#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct JsonField {
    std::string str;
    double num = 0;
    bool boolean = false;
    std::vector<std::string> list;
};

struct JsonObject {
    std::vector<std::pair<std::string, JsonField>> fields;

    const JsonField* find(const char* key) const {
        for (const auto& f : fields) {
            if (f.first == key) return &f.second;
        }
        return nullptr;
    }
    std::string str(const char* key) const {
        const JsonField* f = find(key);
        return f != nullptr ? f->str : std::string{};
    }
    bool boolean(const char* key) const {
        const JsonField* f = find(key);
        return f != nullptr && f->boolean;
    }
    std::vector<std::string> list(const char* key) const {
        const JsonField* f = find(key);
        return f != nullptr ? f->list : std::vector<std::string>{};
    }
};

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : m_text(text) {}

    bool object(JsonObject& out) {
        skip_ws();
        if (!eat('{')) return false;
        skip_ws();
        if (eat('}')) return true;
        for (;;) {
            std::string key;
            skip_ws();
            if (!string(key)) return false;
            skip_ws();
            if (!eat(':')) return false;
            JsonField field;
            if (!value(field)) return false;
            out.fields.emplace_back(std::move(key), std::move(field));
            skip_ws();
            if (eat(',')) continue;
            return eat('}');
        }
    }

private:
    bool value(JsonField& out) {
        skip_ws();
        if (m_pos >= m_text.size()) return false;
        const char c = m_text[m_pos];
        if (c == '"') return string(out.str);
        if (c == '[') {
            ++m_pos;
            skip_ws();
            if (eat(']')) return true;
            for (;;) {
                JsonField item;
                if (!value(item)) return false;
                out.list.push_back(std::move(item.str));
                skip_ws();
                if (eat(',')) continue;
                return eat(']');
            }
        }
        if (c == '{') {
            JsonObject ignored;
            return object(ignored);
        }
        if (m_text.substr(m_pos, 4) == "true") {
            m_pos += 4;
            out.boolean = true;
            return true;
        }
        if (m_text.substr(m_pos, 5) == "false") {
            m_pos += 5;
            return true;
        }
        if (m_text.substr(m_pos, 4) == "null") {
            m_pos += 4;
            return true;
        }
        const size_t start = m_pos;
        while (m_pos < m_text.size() && std::strchr("+-0123456789.eE", m_text[m_pos]) != nullptr) {
            ++m_pos;
        }
        if (m_pos == start) return false;
        out.str = std::string(m_text.substr(start, m_pos - start));
        out.num = std::strtod(out.str.c_str(), nullptr);
        return true;
    }

    bool string(std::string& out) {
        if (!eat('"')) return false;
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_pos >= m_text.size()) return false;
            const char e = m_text[m_pos++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u':

                if (m_pos + 4 > m_text.size()) return false;
                m_pos += 4;
                out += '?';
                break;
            default: out += e; break;
            }
        }
        return false;
    }

    void skip_ws() {
        while (m_pos < m_text.size() && std::strchr(" \t\r\n", m_text[m_pos]) != nullptr) ++m_pos;
    }
    bool eat(char c) {
        if (m_pos < m_text.size() && m_text[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    std::string_view m_text;
    size_t m_pos = 0;
};

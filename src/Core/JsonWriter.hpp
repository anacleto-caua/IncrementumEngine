#pragma once

#include <array>
#include <charconv>
#include <type_traits>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

// Minimal streaming JSON writer into an in-memory string - no DOM, no parsing. Used by the
// benchmark to build a whole JSON Lines record in memory and write it out in one go. Commas are
// tracked per nesting level, so callers only describe structure (Key/value/Begin/End).
// Non-finite floats are written as null, since JSON has no NaN/Inf.
class JsonWriter {
public:
    std::string Out;

    void BeginObject();
    void EndObject();
    void BeginArray();
    void EndArray();

    void Key(std::string_view key);

    void String(std::string_view value);
    void Float(f64 value, i32 precision = 6);
    void Int(i64 value);
    void UInt(u64 value);
    void Bool(bool value);
    void Null();

    // Key + value shorthands.
    void Field(std::string_view key, std::string_view value) { Key(key); String(value); }
    void Field(std::string_view key, const char* value) { Key(key); String(value); }
    void Field(std::string_view key, f64 value) { Key(key); Float(value); }
    void Field(std::string_view key, f32 value) { Key(key); Float(static_cast<f64>(value)); }
    void Field(std::string_view key, i32 value) { Key(key); Int(value); }
    void Field(std::string_view key, i64 value) { Key(key); Int(value); }
    void Field(std::string_view key, u32 value) { Key(key); UInt(value); }
    void Field(std::string_view key, u64 value) { Key(key); UInt(value); }
    void Field(std::string_view key, bool value) { Key(key); Bool(value); }

    // Whole numeric column as one array - the benchmark's per-frame data is written columnar.
    template<typename T>
    void Column(std::string_view key, const std::vector<T>& values, i32 precision = 5);

private:
    std::vector<bool> FirstInScope;
    bool AfterKey = false;

    void Separator();
};

// ====

inline void JsonWriter::Separator() {
    if (AfterKey) {
        AfterKey = false;
        return;
    }
    if (!FirstInScope.empty()) {
        if (!FirstInScope.back()) { Out.push_back(','); }
        FirstInScope.back() = false;
    }
}

inline void JsonWriter::BeginObject() {
    Separator();
    Out.push_back('{');
    FirstInScope.push_back(true);
}

inline void JsonWriter::EndObject() {
    Out.push_back('}');
    FirstInScope.pop_back();
}

inline void JsonWriter::BeginArray() {
    Separator();
    Out.push_back('[');
    FirstInScope.push_back(true);
}

inline void JsonWriter::EndArray() {
    Out.push_back(']');
    FirstInScope.pop_back();
}

inline void JsonWriter::Key(std::string_view key) {
    String(key);
    Out.push_back(':');
    AfterKey = true;
}

inline void JsonWriter::String(std::string_view value) {
    Separator();
    Out.push_back('"');
    for (char c : value) {
        switch (c) {
            case '"':  Out += "\\\""; break;
            case '\\': Out += "\\\\"; break;
            case '\n': Out += "\\n"; break;
            case '\r': Out += "\\r"; break;
            case '\t': Out += "\\t"; break;
            default:
                if (static_cast<u8>(c) < 0x20) {
                    constexpr const char* hex = "0123456789abcdef";
                    Out += "\\u00";
                    Out.push_back(hex[(c >> 4) & 0xF]);
                    Out.push_back(hex[c & 0xF]);
                } else {
                    Out.push_back(c);
                }
        }
    }
    Out.push_back('"');
}

inline void JsonWriter::Float(f64 value, i32 precision) {
    if (!std::isfinite(value)) {
        Null();
        return;
    }
    Separator();
    std::array<char, 64> buffer;
    auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::general, precision);
    Out.append(buffer.data(), result.ptr);
}

inline void JsonWriter::Int(i64 value) {
    Separator();
    std::array<char, 32> buffer;
    auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    Out.append(buffer.data(), result.ptr);
}

inline void JsonWriter::UInt(u64 value) {
    Separator();
    std::array<char, 32> buffer;
    auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    Out.append(buffer.data(), result.ptr);
}

inline void JsonWriter::Bool(bool value) {
    Separator();
    Out += value ? "true" : "false";
}

inline void JsonWriter::Null() {
    Separator();
    Out += "null";
}

template<typename T>
inline void JsonWriter::Column(std::string_view key, const std::vector<T>& values, i32 precision) {
    Key(key);
    BeginArray();
    for (const T& value : values) {
        if constexpr (std::is_floating_point_v<T>) {
            Float(static_cast<f64>(value), precision);
        } else if constexpr (std::is_signed_v<T>) {
            Int(static_cast<i64>(value));
        } else {
            UInt(static_cast<u64>(value));
        }
    }
    EndArray();
}

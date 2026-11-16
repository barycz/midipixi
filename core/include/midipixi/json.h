#pragma once
// Small, non-allocating JSON parser and streaming writer.
//
// * The parser builds a flat node tree in caller-provided storage and never
//   modifies the input text. Strings are referenced in place (raw, with escapes)
//   and unescaped on demand into caller buffers.
// * Number parsing and formatting are locale independent.
// * Keys are compared raw (keys containing escape sequences are not supported).

#include <cstddef>
#include <cstdint>

namespace mpx {

enum class JsonType : uint8_t { Null, Bool, Number, String, Array, Object };

struct JsonNode {
    JsonType type = JsonType::Null;
    bool boolean = false;
    uint32_t keyLen = 0;
    uint32_t strLen = 0;
    const char* key = nullptr;  // member name when the parent is an object
    const char* str = nullptr;  // raw string contents (escapes not processed)
    double number = 0.0;
    int32_t firstChild = -1;
    int32_t next = -1;
    int32_t childCount = 0;
    uint32_t srcBegin = 0;  // offset of the value's first character in the source text
    uint32_t srcEnd = 0;    // offset one past the value's last character
};

class JsonDoc {
public:
    JsonDoc(JsonNode* storage, int capacity) : nodes_(storage), capacity_(capacity) {}

    // Parses a complete JSON text. Returns false on syntax error or when the
    // node storage is exhausted (see error()).
    bool parse(const char* text, size_t len);

    const char* error() const { return error_; }
    size_t errorOffset() const { return errorPos_; }

    int root() const { return count_ > 0 ? 0 : -1; }
    int nodeCount() const { return count_; }
    const JsonNode& node(int i) const { return nodes_[i]; }
    JsonType type(int i) const { return i >= 0 ? nodes_[i].type : JsonType::Null; }

    // Member lookup in an object node; -1 when missing or not an object.
    int find(int object, const char* key) const;
    // Element lookup in an array node; -1 when out of range.
    int at(int array, int index) const;
    int firstChild(int i) const { return i >= 0 ? nodes_[i].firstChild : -1; }
    int next(int i) const { return i >= 0 ? nodes_[i].next : -1; }

    bool isNumber(int i) const { return type(i) == JsonType::Number; }
    bool isString(int i) const { return type(i) == JsonType::String; }

    double number(int i, double def) const { return isNumber(i) ? nodes_[i].number : def; }
    bool boolean(int i, bool def) const { return type(i) == JsonType::Bool ? nodes_[i].boolean : def; }
    // Copies the unescaped string into out (always zero-terminated, truncated
    // to cap-1 bytes). Returns false when the node is not a string.
    bool string(int i, char* out, size_t cap) const;
    // Compares the raw string value with s.
    bool stringEquals(int i, const char* s) const;

    double getNumber(int object, const char* key, double def) const { return number(find(object, key), def); }
    bool getBool(int object, const char* key, bool def) const { return boolean(find(object, key), def); }
    bool getString(int object, const char* key, char* out, size_t cap) const {
        return string(find(object, key), out, cap);
    }

    // Raw source span of a node (whole object/array/value text). Useful for
    // forwarding sub-documents without re-serializing them.
    bool span(int i, const char*& begin, size_t& len) const;

private:
    int newNode(JsonType t);
    int parseValue(int depth);
    bool parseString(const char*& s, uint32_t& len);
    bool parseNumber(double& out);
    void skipWs();
    bool fail(const char* msg);

    JsonNode* nodes_;
    int capacity_;
    int count_ = 0;
    const char* text_ = nullptr;
    size_t len_ = 0;
    size_t pos_ = 0;
    const char* error_ = nullptr;
    size_t errorPos_ = 0;
};

// Locale-independent number formatting: integers print without a fraction,
// other values with up to 6 decimals (trailing zeros trimmed). Returns length.
// out must hold at least 32 bytes.
size_t formatJsonNumber(double v, char* out);

class JsonWriter {
public:
    using Sink = void (*)(void* ctx, const char* data, size_t len);

    JsonWriter(Sink sink, void* ctx, bool pretty = false) : sink_(sink), ctx_(ctx), pretty_(pretty) {}

    JsonWriter& beginObject();
    JsonWriter& endObject();
    JsonWriter& beginArray();
    JsonWriter& endArray();
    JsonWriter& key(const char* k);
    JsonWriter& value(double v);
    JsonWriter& value(float v) { return value(static_cast<double>(v)); }
    JsonWriter& value(int v) { return value(static_cast<double>(v)); }
    JsonWriter& value(unsigned v) { return value(static_cast<double>(v)); }
    JsonWriter& value(bool v);
    JsonWriter& value(const char* s);
    JsonWriter& value(const char* s, size_t len);
    JsonWriter& null();
    // Writes pre-formatted JSON as a value (no validation).
    JsonWriter& raw(const char* json, size_t len);

    template <typename T>
    JsonWriter& field(const char* k, T v) {
        key(k);
        return value(v);
    }

private:
    void beforeValue();
    void writeString(const char* s, size_t len);
    void open(char c);
    void close(char c);
    void newline();
    void put(const char* s, size_t n) { sink_(ctx_, s, n); }
    void put(char c) { sink_(ctx_, &c, 1); }

    static constexpr int kMaxDepth = 32;
    Sink sink_;
    void* ctx_;
    bool pretty_;
    int depth_ = 0;
    bool hasItems_[kMaxDepth] = {};
    bool afterKey_ = false;
};

// Sink writing into a fixed buffer; sets overflow when it runs out of space.
// The buffer is kept zero-terminated.
struct FixedJsonSink {
    char* buf;
    size_t cap;
    size_t len = 0;
    bool overflow = false;

    FixedJsonSink(char* b, size_t c) : buf(b), cap(c) {
        if (cap) buf[0] = 0;
    }
    static void write(void* ctx, const char* data, size_t n);
};

} // namespace mpx

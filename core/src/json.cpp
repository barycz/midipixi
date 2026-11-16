#include "midipixi/json.h"

#include <cstring>

namespace mpx {

namespace {

constexpr int kMaxParseDepth = 32;

bool isDigit(char c) { return c >= '0' && c <= '9'; }

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

size_t encodeUtf8(uint32_t cp, char* out) {
    if (cp < 0x80) {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800) {
        out[0] = static_cast<char>(0xC0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (cp >> 18));
    out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
}

double pow10i(int e) {
    double r = 1.0, b = 10.0;
    bool neg = e < 0;
    unsigned u = static_cast<unsigned>(neg ? -e : e);
    while (u) {
        if (u & 1) r *= b;
        b *= b;
        u >>= 1;
    }
    return neg ? 1.0 / r : r;
}

} // namespace

bool JsonDoc::fail(const char* msg) {
    if (!error_) {
        error_ = msg;
        errorPos_ = pos_;
    }
    return false;
}

void JsonDoc::skipWs() {
    while (pos_ < len_) {
        char c = text_[pos_];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            ++pos_;
        else
            break;
    }
}

int JsonDoc::newNode(JsonType t) {
    if (count_ >= capacity_) {
        fail("too many JSON nodes");
        return -1;
    }
    JsonNode& n = nodes_[count_];
    n = JsonNode{};
    n.type = t;
    n.srcBegin = static_cast<uint32_t>(pos_);
    return count_++;
}

bool JsonDoc::parse(const char* text, size_t len) {
    text_ = text;
    len_ = len;
    pos_ = 0;
    count_ = 0;
    error_ = nullptr;
    errorPos_ = 0;
    skipWs();
    if (parseValue(0) < 0) {
        count_ = 0;
        return false;
    }
    skipWs();
    if (pos_ != len_) {
        // Allow trailing zero bytes (fixed buffers).
        while (pos_ < len_ && text_[pos_] == 0) ++pos_;
        if (pos_ != len_) {
            fail("unexpected trailing characters");
            count_ = 0;
            return false;
        }
    }
    return true;
}

bool JsonDoc::parseString(const char*& s, uint32_t& len) {
    // pos_ at opening quote
    ++pos_;
    size_t start = pos_;
    while (pos_ < len_) {
        char c = text_[pos_];
        if (c == '"') {
            s = text_ + start;
            len = static_cast<uint32_t>(pos_ - start);
            ++pos_;
            return true;
        }
        if (c == '\\') {
            pos_ += 2;
            continue;
        }
        if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
        ++pos_;
    }
    return fail("unterminated string");
}

bool JsonDoc::parseNumber(double& out) {
    size_t p = pos_;
    bool neg = false;
    if (p < len_ && text_[p] == '-') {
        neg = true;
        ++p;
    }
    if (p >= len_ || !isDigit(text_[p])) return fail("invalid number");
    double mant = 0.0;
    int exp10 = 0;
    while (p < len_ && isDigit(text_[p])) {
        mant = mant * 10.0 + (text_[p] - '0');
        ++p;
    }
    if (p < len_ && text_[p] == '.') {
        ++p;
        if (p >= len_ || !isDigit(text_[p])) return fail("invalid number");
        while (p < len_ && isDigit(text_[p])) {
            mant = mant * 10.0 + (text_[p] - '0');
            --exp10;
            ++p;
        }
    }
    if (p < len_ && (text_[p] == 'e' || text_[p] == 'E')) {
        ++p;
        bool eneg = false;
        if (p < len_ && (text_[p] == '+' || text_[p] == '-')) {
            eneg = text_[p] == '-';
            ++p;
        }
        if (p >= len_ || !isDigit(text_[p])) return fail("invalid number");
        int e = 0;
        while (p < len_ && isDigit(text_[p])) {
            if (e < 10000) e = e * 10 + (text_[p] - '0');
            ++p;
        }
        exp10 += eneg ? -e : e;
    }
    pos_ = p;
    double v = exp10 ? mant * pow10i(exp10) : mant;
    out = neg ? -v : v;
    return true;
}

int JsonDoc::parseValue(int depth) {
    if (depth > kMaxParseDepth) {
        fail("nesting too deep");
        return -1;
    }
    if (pos_ >= len_) {
        fail("unexpected end of input");
        return -1;
    }
    char c = text_[pos_];
    int idx;
    switch (c) {
        case '{':
        case '[': {
            bool isObj = c == '{';
            idx = newNode(isObj ? JsonType::Object : JsonType::Array);
            if (idx < 0) return -1;
            ++pos_;
            skipWs();
            char closeCh = isObj ? '}' : ']';
            if (pos_ < len_ && text_[pos_] == closeCh) {
                ++pos_;
                nodes_[idx].srcEnd = static_cast<uint32_t>(pos_);
                return idx;
            }
            int prev = -1;
            for (;;) {
                const char* key = nullptr;
                uint32_t keyLen = 0;
                if (isObj) {
                    skipWs();
                    if (pos_ >= len_ || text_[pos_] != '"') {
                        fail("expected member name");
                        return -1;
                    }
                    if (!parseString(key, keyLen)) return -1;
                    skipWs();
                    if (pos_ >= len_ || text_[pos_] != ':') {
                        fail("expected ':'");
                        return -1;
                    }
                    ++pos_;
                }
                skipWs();
                int child = parseValue(depth + 1);
                if (child < 0) return -1;
                nodes_[child].key = key;
                nodes_[child].keyLen = keyLen;
                if (prev < 0)
                    nodes_[idx].firstChild = child;
                else
                    nodes_[prev].next = child;
                prev = child;
                nodes_[idx].childCount++;
                skipWs();
                if (pos_ >= len_) {
                    fail("unexpected end of input");
                    return -1;
                }
                if (text_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (text_[pos_] == closeCh) {
                    ++pos_;
                    break;
                }
                fail(isObj ? "expected ',' or '}'" : "expected ',' or ']'");
                return -1;
            }
            nodes_[idx].srcEnd = static_cast<uint32_t>(pos_);
            return idx;
        }
        case '"': {
            idx = newNode(JsonType::String);
            if (idx < 0) return -1;
            const char* s;
            uint32_t l;
            if (!parseString(s, l)) return -1;
            nodes_[idx].str = s;
            nodes_[idx].strLen = l;
            nodes_[idx].srcEnd = static_cast<uint32_t>(pos_);
            return idx;
        }
        case 't':
        case 'f':
        case 'n': {
            const char* word = c == 't' ? "true" : (c == 'f' ? "false" : "null");
            size_t wl = std::strlen(word);
            if (pos_ + wl > len_ || std::memcmp(text_ + pos_, word, wl) != 0) {
                fail("invalid literal");
                return -1;
            }
            idx = newNode(c == 'n' ? JsonType::Null : JsonType::Bool);
            if (idx < 0) return -1;
            nodes_[idx].boolean = c == 't';
            pos_ += wl;
            nodes_[idx].srcEnd = static_cast<uint32_t>(pos_);
            return idx;
        }
        default: {
            idx = newNode(JsonType::Number);
            if (idx < 0) return -1;
            double v;
            if (!parseNumber(v)) return -1;
            nodes_[idx].number = v;
            nodes_[idx].srcEnd = static_cast<uint32_t>(pos_);
            return idx;
        }
    }
}

int JsonDoc::find(int object, const char* key) const {
    if (object < 0 || nodes_[object].type != JsonType::Object) return -1;
    size_t kl = std::strlen(key);
    for (int c = nodes_[object].firstChild; c >= 0; c = nodes_[c].next) {
        const JsonNode& n = nodes_[c];
        if (n.keyLen == kl && std::memcmp(n.key, key, kl) == 0) return c;
    }
    return -1;
}

int JsonDoc::at(int array, int index) const {
    if (array < 0 || nodes_[array].type != JsonType::Array || index < 0) return -1;
    int c = nodes_[array].firstChild;
    while (c >= 0 && index-- > 0) c = nodes_[c].next;
    return c;
}

bool JsonDoc::string(int i, char* out, size_t cap) const {
    if (!isString(i) || cap == 0) return false;
    const JsonNode& n = nodes_[i];
    size_t o = 0;
    for (uint32_t k = 0; k < n.strLen;) {
        char c = n.str[k];
        char tmp[4];
        size_t tl = 1;
        if (c != '\\') {
            tmp[0] = c;
            ++k;
        } else {
            char e = k + 1 < n.strLen ? n.str[k + 1] : 0;
            k += 2;
            switch (e) {
                case 'n': tmp[0] = '\n'; break;
                case 't': tmp[0] = '\t'; break;
                case 'r': tmp[0] = '\r'; break;
                case 'b': tmp[0] = '\b'; break;
                case 'f': tmp[0] = '\f'; break;
                case 'u': {
                    uint32_t cp = 0;
                    for (int h = 0; h < 4 && k < n.strLen; ++h, ++k) {
                        int hv = hexValue(n.str[k]);
                        cp = (cp << 4) | static_cast<uint32_t>(hv < 0 ? 0 : hv);
                    }
                    // Surrogate pair
                    if (cp >= 0xD800 && cp < 0xDC00 && k + 6 <= n.strLen && n.str[k] == '\\' && n.str[k + 1] == 'u') {
                        uint32_t lo = 0;
                        for (int h = 0; h < 4; ++h) {
                            int hv = hexValue(n.str[k + 2 + h]);
                            lo = (lo << 4) | static_cast<uint32_t>(hv < 0 ? 0 : hv);
                        }
                        if (lo >= 0xDC00 && lo < 0xE000) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            k += 6;
                        }
                    }
                    tl = encodeUtf8(cp, tmp);
                    break;
                }
                default: tmp[0] = e; break;  // \" \\ \/
            }
        }
        if (o + tl >= cap) break;
        std::memcpy(out + o, tmp, tl);
        o += tl;
    }
    out[o] = 0;
    return true;
}

bool JsonDoc::stringEquals(int i, const char* s) const {
    if (!isString(i)) return false;
    size_t l = std::strlen(s);
    return nodes_[i].strLen == l && std::memcmp(nodes_[i].str, s, l) == 0;
}

bool JsonDoc::span(int i, const char*& begin, size_t& len) const {
    if (i < 0 || i >= count_) return false;
    begin = text_ + nodes_[i].srcBegin;
    len = nodes_[i].srcEnd - nodes_[i].srcBegin;
    return true;
}

// ---------------------------------------------------------------------------

size_t formatJsonNumber(double v, char* out) {
    size_t o = 0;
    if (!(v == v)) {  // NaN is not representable in JSON
        out[0] = '0';
        out[1] = 0;
        return 1;
    }
    if (v < 0) {
        out[o++] = '-';
        v = -v;
    }
    if (v > 9.0e15) v = 9.0e15;
    unsigned long long ip = static_cast<unsigned long long>(v);
    double frac = v - static_cast<double>(ip);
    unsigned long long fr = static_cast<unsigned long long>(frac * 1000000.0 + 0.5);
    if (fr >= 1000000ULL) {
        ++ip;
        fr -= 1000000ULL;
    }
    if (ip == 0 && fr == 0 && o == 1) o = 0;  // "-0" -> "0"
    char digits[24];
    int nd = 0;
    do {
        digits[nd++] = static_cast<char>('0' + ip % 10);
        ip /= 10;
    } while (ip);
    while (nd) out[o++] = digits[--nd];
    if (fr) {
        out[o++] = '.';
        char f[6];
        for (int i = 5; i >= 0; --i) {
            f[i] = static_cast<char>('0' + fr % 10);
            fr /= 10;
        }
        int last = 5;
        while (last > 0 && f[last] == '0') --last;
        for (int i = 0; i <= last; ++i) out[o++] = f[i];
    }
    out[o] = 0;
    return o;
}

void JsonWriter::newline() {
    if (!pretty_) return;
    put('\n');
    for (int i = 0; i < depth_; ++i) put("  ", 2);
}

void JsonWriter::beforeValue() {
    if (afterKey_) {
        afterKey_ = false;
        return;
    }
    if (depth_ > 0) {
        if (hasItems_[depth_ - 1]) put(',');
        hasItems_[depth_ - 1] = true;
        newline();
    }
}

void JsonWriter::open(char c) {
    beforeValue();
    put(c);
    if (depth_ < kMaxDepth) hasItems_[depth_] = false;
    ++depth_;
}

void JsonWriter::close(char c) {
    --depth_;
    bool had = depth_ < kMaxDepth && hasItems_[depth_];
    if (had) newline();
    put(c);
}

JsonWriter& JsonWriter::beginObject() {
    open('{');
    return *this;
}
JsonWriter& JsonWriter::endObject() {
    close('}');
    return *this;
}
JsonWriter& JsonWriter::beginArray() {
    open('[');
    return *this;
}
JsonWriter& JsonWriter::endArray() {
    close(']');
    return *this;
}

JsonWriter& JsonWriter::key(const char* k) {
    beforeValue();
    writeString(k, std::strlen(k));
    put(':');
    if (pretty_) put(' ');
    afterKey_ = true;
    return *this;
}

JsonWriter& JsonWriter::value(double v) {
    beforeValue();
    char buf[32];
    size_t n = formatJsonNumber(v, buf);
    put(buf, n);
    return *this;
}

JsonWriter& JsonWriter::value(bool v) {
    beforeValue();
    if (v)
        put("true", 4);
    else
        put("false", 5);
    return *this;
}

JsonWriter& JsonWriter::value(const char* s) { return value(s, s ? std::strlen(s) : 0); }

JsonWriter& JsonWriter::value(const char* s, size_t len) {
    beforeValue();
    writeString(s, len);
    return *this;
}

void JsonWriter::writeString(const char* s, size_t len) {
    put('"');
    size_t runStart = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        const char* esc = nullptr;
        char ubuf[7];
        if (c == '"')
            esc = "\\\"";
        else if (c == '\\')
            esc = "\\\\";
        else if (c == '\n')
            esc = "\\n";
        else if (c == '\r')
            esc = "\\r";
        else if (c == '\t')
            esc = "\\t";
        else if (c < 0x20) {
            static const char hex[] = "0123456789abcdef";
            ubuf[0] = '\\';
            ubuf[1] = 'u';
            ubuf[2] = '0';
            ubuf[3] = '0';
            ubuf[4] = hex[c >> 4];
            ubuf[5] = hex[c & 15];
            ubuf[6] = 0;
            esc = ubuf;
        }
        if (esc) {
            if (i > runStart) put(s + runStart, i - runStart);
            put(esc, std::strlen(esc));
            runStart = i + 1;
        }
    }
    if (len > runStart) put(s + runStart, len - runStart);
    put('"');
}

JsonWriter& JsonWriter::null() {
    beforeValue();
    put("null", 4);
    return *this;
}

JsonWriter& JsonWriter::raw(const char* json, size_t len) {
    beforeValue();
    put(json, len);
    return *this;
}

void FixedJsonSink::write(void* ctx, const char* data, size_t n) {
    auto* s = static_cast<FixedJsonSink*>(ctx);
    if (s->overflow) return;
    if (s->len + n + 1 > s->cap) {
        s->overflow = true;
        return;
    }
    std::memcpy(s->buf + s->len, data, n);
    s->len += n;
    s->buf[s->len] = 0;
}

} // namespace mpx

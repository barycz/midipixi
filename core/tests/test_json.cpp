#include <clocale>
#include <cstring>
#include <string>
#include <vector>

#include "midipixi/json.h"
#include "mpx_test.h"

using namespace mpx;

namespace {

void stringSink(void* ctx, const char* d, size_t n) { static_cast<std::string*>(ctx)->append(d, n); }

std::string fmt(double v) {
    char buf[32];
    formatJsonNumber(v, buf);
    return buf;
}

} // namespace

TEST_CASE("json: parse nested document") {
    const char* text = R"({"a": 1.5, "b": [1, 2, {"c": "x\"y"}], "t": true, "f": false, "n": null, "e": -2e-3})";
    std::vector<JsonNode> nodes(64);
    JsonDoc d(nodes.data(), static_cast<int>(nodes.size()));
    REQUIRE(d.parse(text, std::strlen(text)));
    int root = d.root();
    CHECK_NEAR(d.getNumber(root, "a", 0), 1.5, 1e-12);
    int b = d.find(root, "b");
    CHECK(d.node(b).childCount == 3);
    CHECK_NEAR(d.number(d.at(b, 1), 0), 2.0, 0);
    char s[16];
    CHECK(d.getString(d.at(b, 2), "c", s, sizeof(s)));
    CHECK(std::strcmp(s, "x\"y") == 0);
    CHECK(d.getBool(root, "t", false));
    CHECK(!d.getBool(root, "f", true));
    CHECK(d.type(d.find(root, "n")) == JsonType::Null);
    CHECK_NEAR(d.getNumber(root, "e", 0), -0.002, 1e-12);
    CHECK(d.find(root, "missing") == -1);
}

TEST_CASE("json: unicode escapes decode to utf-8") {
    const char* text = R"(["é😀\n"])";
    JsonNode nodes[4];
    JsonDoc d(nodes, 4);
    REQUIRE(d.parse(text, std::strlen(text)));
    char s[16];
    REQUIRE(d.string(d.at(d.root(), 0), s, sizeof(s)));
    CHECK(std::strcmp(s, "\xC3\xA9\xF0\x9F\x98\x80\n") == 0);
}

TEST_CASE("json: syntax errors are reported") {
    const char* bad[] = {"{", "[1,]", "{\"a\" 1}", "tru", "\"abc", "[1] x", "{\"a\":01x}"};
    for (const char* t : bad) {
        JsonNode nodes[16];
        JsonDoc d(nodes, 16);
        CHECK(!d.parse(t, std::strlen(t)));
        CHECK(d.error() != nullptr);
    }
}

TEST_CASE("json: node capacity exhaustion fails cleanly") {
    const char* text = "[1,2,3,4,5,6,7,8]";
    JsonNode nodes[4];
    JsonDoc d(nodes, 4);
    CHECK(!d.parse(text, std::strlen(text)));
}

TEST_CASE("json: number formatting is locale independent") {
    // Simulate a host that switched to a decimal-comma locale (if available).
    std::setlocale(LC_NUMERIC, "de_DE.UTF-8");
    std::setlocale(LC_NUMERIC, "German_Germany.1252");
    CHECK(fmt(0.5) == "0.5");
    CHECK(fmt(-0.005) == "-0.005");
    CHECK(fmt(3) == "3");
    CHECK(fmt(0.3f) == "0.3");
    CHECK(fmt(-0.0000001) == "0");
    CHECK(fmt(1234567.25) == "1234567.25");
    const char* text = "[0.25]";
    JsonNode nodes[4];
    JsonDoc d(nodes, 4);
    REQUIRE(d.parse(text, std::strlen(text)));
    CHECK_NEAR(d.number(d.at(d.root(), 0), 0), 0.25, 1e-12);
    std::setlocale(LC_NUMERIC, "C");
}

TEST_CASE("json: writer produces parseable output") {
    std::string out;
    JsonWriter w(stringSink, &out, true);
    w.beginObject();
    w.field("name", "a \"quoted\"\tname");
    w.key("list").beginArray().value(1).value(2.5).null().endArray();
    w.key("empty").beginObject().endObject();
    w.field("ok", true);
    w.endObject();
    std::vector<JsonNode> nodes(32);
    JsonDoc d(nodes.data(), 32);
    REQUIRE(d.parse(out.data(), out.size()));
    char s[32];
    CHECK(d.getString(d.root(), "name", s, sizeof(s)));
    CHECK(std::strcmp(s, "a \"quoted\"\tname") == 0);
    CHECK(d.node(d.find(d.root(), "list")).childCount == 3);
    CHECK(d.node(d.find(d.root(), "empty")).childCount == 0);

    std::string compact;
    JsonWriter c(stringSink, &compact, false);
    c.beginObject().field("a", 1).key("b").beginArray().value(2).endArray().endObject();
    CHECK(compact == "{\"a\":1,\"b\":[2]}");
}

TEST_CASE("json: span returns the raw sub-document") {
    const char* text = R"({"cmd":"put","preset":{"name":"x","layers":[]}})";
    JsonNode nodes[16];
    JsonDoc d(nodes, 16);
    REQUIRE(d.parse(text, std::strlen(text)));
    const char* b;
    size_t l;
    REQUIRE(d.span(d.find(d.root(), "preset"), b, l));
    CHECK(std::string(b, l) == R"({"name":"x","layers":[]})");
}

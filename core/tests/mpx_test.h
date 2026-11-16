#pragma once
// Minimal dependency-free test harness for core tests.

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace mpxtest {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

inline int& failures() {
    static int f = 0;
    return f;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void fail(const char* file, int line, const std::string& what) {
    std::printf("  FAIL %s:%d: %s\n", file, line, what.c_str());
    ++failures();
}

inline int runAll(const char* filter) {
    int run = 0, failedCases = 0;
    for (auto& t : registry()) {
        if (filter && std::string(t.name).find(filter) == std::string::npos) continue;
        int before = failures();
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        ++run;
        if (failures() != before) {
            ++failedCases;
            std::printf("[ FAIL ] %s\n", t.name);
        }
    }
    std::printf("%d test cases, %d failed, %d failed checks\n", run, failedCases, failures());
    return failedCases == 0 ? 0 : 1;
}

} // namespace mpxtest

#define MPX_CAT2(a, b) a##b
#define MPX_CAT(a, b) MPX_CAT2(a, b)
#define TEST_CASE(name)                                                                 \
    static void MPX_CAT(mpx_test_fn_, __LINE__)();                                      \
    static mpxtest::Registrar MPX_CAT(mpx_test_reg_, __LINE__)(name, &MPX_CAT(mpx_test_fn_, __LINE__)); \
    static void MPX_CAT(mpx_test_fn_, __LINE__)()

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) mpxtest::fail(__FILE__, __LINE__, #cond);            \
    } while (0)

#define REQUIRE(cond)                                                     \
    do {                                                                  \
        if (!(cond)) {                                                    \
            mpxtest::fail(__FILE__, __LINE__, #cond);                     \
            return;                                                       \
        }                                                                 \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                  \
    do {                                                                                       \
        double mpx_a_ = (a), mpx_b_ = (b);                                                     \
        if (!(std::fabs(mpx_a_ - mpx_b_) <= (eps)))                                            \
            mpxtest::fail(__FILE__, __LINE__,                                                  \
                          std::string(#a " ~= " #b "  (") + std::to_string(mpx_a_) + " vs " +   \
                              std::to_string(mpx_b_) + ")");                                   \
    } while (0)

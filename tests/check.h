#pragma once
// Minimal test harness: TEST(name) registers a test, CHECK / CHECK_NEAR record failures.
#include <cmath>
#include <cstdio>
#include <exception>
#include <vector>

struct TestCase { const char* name; void (*run)(); };
inline std::vector<TestCase>& allTests() { static std::vector<TestCase> tests; return tests; }
inline int& failureCount() { static int count = 0; return count; }
struct TestRegistrar { TestRegistrar(const char* name, void (*run)()) { allTests().push_back({name, run}); } };

#define TEST(name) \
    static void name(); \
    static TestRegistrar registrar_##name(#name, name); \
    static void name()

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); ++failureCount(); } } while (0)

#define CHECK_THROWS(expr) \
    do { \
        bool threw_ = false; \
        try { expr; } catch (const std::exception&) { threw_ = true; } \
        if (!threw_) { std::printf("%s:%d: CHECK_THROWS failed: %s did not throw\n", __FILE__, __LINE__, #expr); ++failureCount(); } \
    } while (0)

#define CHECK_NEAR(a, b, eps) \
    do { \
        const double a_ = (a), b_ = (b); \
        if (!(std::fabs(a_ - b_) <= (eps))) { \
            std::printf("%s:%d: CHECK_NEAR failed: %s = %.12g, %s = %.12g\n", __FILE__, __LINE__, #a, a_, #b, b_); \
            ++failureCount(); \
        } \
    } while (0)

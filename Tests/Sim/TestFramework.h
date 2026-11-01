#pragma once
// Tiny dependency-free test framework (no network needed to build tests).
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace ScavarTest
{
struct Case { const char* Name; std::function<void()> Fn; };
inline std::vector<Case>& Registry() { static std::vector<Case> r; return r; }
inline int& Failures() { static int f = 0; return f; }
struct Registrar { Registrar(const char* n, std::function<void()> f) { Registry().push_back({n, std::move(f)}); } };
} // namespace ScavarTest

#define TEST(name) \
    static void test_##name(); \
    static ScavarTest::Registrar registrar_##name(#name, test_##name); \
    static void test_##name()

#define CHECK(cond) do { if (!(cond)) { std::printf("    FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); ++ScavarTest::Failures(); } } while (0)
#define CHECK_EQ(a, b) do { auto va_ = (a); auto vb_ = (b); if (!(va_ == vb_)) { std::printf("    FAIL %s:%d  CHECK_EQ(%s, %s)\n", __FILE__, __LINE__, #a, #b); ++ScavarTest::Failures(); } } while (0)
#define CHECK_NEAR(a, b, tol) do { double va_ = (a), vb_ = (b); if (!(std::fabs(va_ - vb_) <= (tol))) { std::printf("    FAIL %s:%d  CHECK_NEAR(%s=%g, %s=%g, tol=%g)\n", __FILE__, __LINE__, #a, va_, #b, vb_, (double)(tol)); ++ScavarTest::Failures(); } } while (0)
